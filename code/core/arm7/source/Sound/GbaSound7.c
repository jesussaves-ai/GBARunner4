#include "common.h"
#include <libtwl/ipc/ipcSync.h>
#include "GbaTimer.h"
#include "GbSound.h"
#include "GbaSound.h"
#include "GbaSoundDefinitions.h"
#include "GbaSound7.h"

gbas_shared_t* gSoundSharedData;
static gbat_t sTimers[2];
static gbas_direct_channel7_t sDirectChannels[2];
static u16 sSoundCntH;
static bool sPaused;

// Upper bound on the GBA direct-sound timer overflows consumed for one DS output
// sample. Direct sound normally runs at or below 2x the DS rate (2 overflows);
// anything beyond this is a timer reload/control change or a stall and is
// clamped so the mixer cannot dump a whole buffer (or hold a stale sample) in a
// single step.
#define GBAS_MAX_OVERFLOWS_PER_SAMPLE 4

// Consecutive DS output samples the mixer holds the last DAC sample when no FIFO
// data is available. A momentary underrun (a handful of samples) is held exactly
// like the real DAC, so normal playback stays bit-accurate; only a stream that
// has genuinely stopped for this long (~8 ms at the 32.7 kHz output rate) begins
// releasing its stuck DC level toward zero.
#define GBAS_FIFO_HOLD_SAMPLES 256

// Maximum step (in s8 units) applied per DS sample while a reset/recovered
// stream is blended back in, and how many samples that blend lasts, so an
// underrun / timer-reload edge can never be emitted as a square-wave step (an
// audible click).
#define GBAS_DECLICK_STEP 8
#define GBAS_DECLICK_SAMPLES 16

// The FIFO half-empty refill request (DRQ) is level-triggered on real hardware:
// as soon as the FIFO has >= 4 words free it must be armed, independent of
// whether a direct-sound sample was consumed for this DS sample. Arming it from
// the zero-overflow path keeps the shallow 8-word FIFO topped up while the guest
// is reprogramming the direct-sound timer (map transitions retrigger the
// tempo/timer), instead of waiting for a later overflow to re-arm the request.
static inline void directChannelRequestRefill(gbas_direct_channel_t* channel)
{
    u32 fifoCount = (channel->writeOffset - channel->readOffset) & GBAS_DIRECT_FIFO_MASK;
    if (!(fifoCount & 4) && !channel->dmaRequest)
    {
        channel->dmaRequest = true;
        ipc_triggerArm7Irq();
    }
}

// Produce one DS DAC output sample. `underrun` is true when no new FIFO sample
// was consumed for it. A short underrun returns the held sample unchanged
// (hardware sample-and-hold); a long one releases the stuck DC logarithmically.
// A recovered or restarted stream is slewed toward the new value instead of
// stepping, so a map-transition reload cannot emit a click.
static inline s8 directChannelEmit(gbas_direct_channel7_t* channel7, s8 raw, bool underrun)
{
    if (underrun)
    {
        if (channel7->underrunCount != 0xFFFFFFFF)
            channel7->underrunCount++;
        if (channel7->underrunCount > GBAS_FIFO_HOLD_SAMPLES)
            channel7->lastOut -= channel7->lastOut >> 3;
        return (s8)channel7->lastOut;
    }

    // Fresh data after a long hold: blend the recovery instead of stepping.
    if (__builtin_expect(channel7->underrunCount > GBAS_FIFO_HOLD_SAMPLES, false))
        channel7->slewCount = GBAS_DECLICK_SAMPLES;

    s8 out = raw;
    if (__builtin_expect(channel7->slewCount != 0, false))
    {
        int diff = (int)raw - channel7->lastOut;
        if (diff > GBAS_DECLICK_STEP)
            diff = GBAS_DECLICK_STEP;
        else if (diff < -GBAS_DECLICK_STEP)
            diff = -GBAS_DECLICK_STEP;
        out = (s8)(channel7->lastOut + diff);
        if (diff == 0)
            channel7->slewCount = 0;
        else
            channel7->slewCount--;
    }
    channel7->underrunCount = 0;
    channel7->lastOut = out;
    return out;
}

static s8 updateDirectChannel(gbas_direct_channel_t* channel, gbas_direct_channel7_t* channel7, u32 timerOverflows)
{
    u32 samples = channel7->curPlaySamples;
    // A SOUNDCNT_H reset strobe cleared this FIFO. Start a fresh FIFO word on
    // the next timer overflow, but DO NOT force the held output sample to 0:
    // real hardware holds the last DAC sample across a FIFO reset, and inserting
    // a hard zero is an audible click. (Forcing 0 here caused crackle in
    // streaming drivers that reset the FIFO mid-track.)
    if (channel->fifoReset)
    {
        channel->fifoReset = false;
        channel7->curPlaySampleCount = 0;
        // The new stream can begin at a very different level than the last DAC
        // sample; arm the de-click blend so the restart is crossfaded, not
        // stepped. Skip it coming out of silence so a fresh attack is not dulled.
        if (channel7->lastOut != 0)
            channel7->slewCount = GBAS_DECLICK_SAMPLES;
    }
    // Nothing elapsed for this DS sample: the output itself is sample-and-hold.
    // The half-empty refill request is still armed here because it is level-
    // triggered on hardware: a FIFO that drained while the direct-sound timer was
    // being reprogrammed (map transitions retrigger tempo/timer) must be refilled
    // now, not only on a later overflow.
    if (timerOverflows == 0)
    {
        directChannelRequestRefill(channel);
        // While holding or blending, keep emitting the shaped value rather than
        // jumping back to the raw held sample.
        if (__builtin_expect(channel7->underrunCount != 0 || channel7->slewCount != 0, false))
            return (s8)channel7->lastOut;
        channel7->lastOut = (s8)samples;
        return (s8)samples;
    }

    // Safety clamp: a Timer 0/1 reload or enable/disable that changes rapidly
    // (high-activity sound effects retriggering the direct-sound timer) or a
    // scheduler stall can make the GBA timer model report an implausible number
    // of overflows for one DS sample. Previously this step was dropped, which
    // held the last sample (a pop) and let the FIFO drain (an underrun). Clamp
    // the catch-up instead so the stream keeps advancing smoothly and the FIFO
    // cannot be emptied in a single step.
    if (timerOverflows > GBAS_MAX_OVERFLOWS_PER_SAMPLE)
    {
        timerOverflows = GBAS_MAX_OVERFLOWS_PER_SAMPLE;
    }

    bool underrun = false;
    for (u32 i = timerOverflows; i > 0; --i)
    {
        // Half-empty refill request, exactly like the GBA FIFO's request point.
        // It also fires on an empty FIFO, so an underrun always re-arms a refill.
        directChannelRequestRefill(channel);
        u32 readOffset = channel->readOffset;
        u32 fifoCount = (channel->writeOffset - readOffset) & GBAS_DIRECT_FIFO_MASK;
        if (channel7->curPlaySampleCount != 0)
        {
            samples >>= 8;
            channel7->curPlaySampleCount--;
        }
        else if (__builtin_expect(fifoCount != 0, true))
        {
            samples = channel->fifo[readOffset];
            channel7->curPlaySampleCount = 3;
            channel->readOffset = (readOffset + 1) & GBAS_DIRECT_FIFO_MASK;
        }
        else
        {
            // FIFO empty: this overflow has no data. Remember it so the output is
            // held (and a long stall released) instead of emitting a bogus step.
            underrun = true;
        }
    }
    channel7->curPlaySamples = samples;

    return directChannelEmit(channel7, (s8)samples, underrun);
}

static inline s16 clampSample(int inSample)
{
    int outSample = inSample << 23;
    if (inSample != (outSample >> 23))
        outSample = 0x7FFFFFFF ^ (inSample >> 31);
    return outSample >> 16;
}

void gbas_init(gbas_shared_t* sharedData)
{
    gSoundSharedData = sharedData;
    gbat_initTimer(&sTimers[0]);
    gbat_initTimer(&sTimers[1]);
    gbs_init();
    sPaused = false; //true;
    sDirectChannels[0].curPlaySampleCount = 0;
    sDirectChannels[0].curPlaySamples = 0;
    sDirectChannels[0].underrunCount = 0;
    sDirectChannels[0].slewCount = 0;
    sDirectChannels[0].lastOut = 0;
    sDirectChannels[1].curPlaySampleCount = 0;
    sDirectChannels[1].curPlaySamples = 0;
    sDirectChannels[1].underrunCount = 0;
    sDirectChannels[1].slewCount = 0;
    sDirectChannels[1].lastOut = 0;
}

void gbas_setTimerReload(u32 timer, u16 reload)
{
    sTimers[timer].reload = reload;
}

void gbas_setTimerControl(u32 timer, u16 control)
{
    sTimers[timer].control = control;
    if (!(control & GBAT_CONTROL_ENABLED))
    {
        sTimers[timer].isStarted = false;
    }
}

void gbas_setSoundCntHLo(u8 value)
{
    ((u8*)&sSoundCntH)[0] = value;
}

void gbas_setSoundCntHHi(u8 value)
{
    ((u8*)&sSoundCntH)[1] = value & 0x77;
}

void gbas_updateMixer(s16* outLeft, s16* outRight)
{
    s16 finalLeft = 0;
    s16 finalRight = 0;

    if (!sPaused)
    {
        u32 timer0Overflows = gbat_updateTimer(&sTimers[0], 0);
        u32 timer1Overflows = gbat_updateTimer(&sTimers[1], timer0Overflows);
        
        //master enable
        if (gSoundSharedData->masterEnable)
        {
            u32 soundCntH = sSoundCntH;
            int sampA = updateDirectChannel(&gSoundSharedData->directChannels[0],
                &sDirectChannels[0],
                (soundCntH & GBA_SOUNDCNT_H_DIRECT_A_TIMER_1) ? timer1Overflows : timer0Overflows);
            int sampB = updateDirectChannel(&gSoundSharedData->directChannels[1],
                &sDirectChannels[1],
                (soundCntH & GBA_SOUNDCNT_H_DIRECT_B_TIMER_1) ? timer1Overflows : timer0Overflows);

            if (soundCntH & GBA_SOUNDCNT_H_DIRECT_A_VOLUME_FULL)
                sampA <<= 1;

            int left = 0;
            int right = 0;
            if (soundCntH & GBA_SOUNDCNT_H_DIRECT_A_ENABLE_LEFT)
                left += sampA;
            if (soundCntH & GBA_SOUNDCNT_H_DIRECT_A_ENABLE_RIGHT)
                right += sampA;

            if (soundCntH & GBA_SOUNDCNT_H_DIRECT_B_VOLUME_FULL)
                sampB <<= 1;

            if (soundCntH & GBA_SOUNDCNT_H_DIRECT_B_ENABLE_LEFT)
                left += sampB;
            if (soundCntH & GBA_SOUNDCNT_H_DIRECT_B_ENABLE_RIGHT)
                right += sampB;

            finalLeft = clampSample(left);
            finalRight = clampSample(right);
        }
    }

    *outLeft = finalLeft;
    *outRight = finalRight;
}