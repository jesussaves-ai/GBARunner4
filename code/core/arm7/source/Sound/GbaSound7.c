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
    }
    // Nothing elapsed for this DS sample: sample-and-hold is correct.
    if (timerOverflows == 0)
    {
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

    for (u32 i = timerOverflows; i > 0; --i)
    {
        u32 readOffset = channel->readOffset;
        u32 fifoCount = (channel->writeOffset - readOffset) & GBAS_DIRECT_FIFO_MASK;
        // Issue a request once at least 4 words are free so the 4-word sound DMA
        // burst always fits, exactly like the GBA FIFO's half-empty request
        // point. The same check also fires on an empty FIFO, so an underrun
        // always re-arms a refill request.
        if (!(fifoCount & 4) && !channel->dmaRequest)
        {
            channel->dmaRequest = true;
            ipc_triggerArm7Irq();
        }
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
    }
    channel7->curPlaySamples = samples;

    return (s8)samples;
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
    sDirectChannels[1].curPlaySampleCount = 0;
    sDirectChannels[1].curPlaySamples = 0;
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