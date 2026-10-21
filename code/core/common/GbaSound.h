#pragma once

// ARM7-side playback state for one Direct Sound channel.
typedef struct
{
    u32 curPlaySampleCount;
    u32 curPlaySamples;

    // DAC de-click state. Real hardware holds the last written sample when the
    // FIFO underruns; that is correct for a momentary underrun, but a stream that
    // has genuinely stopped would otherwise leave a stuck DC level. `underrunCount`
    // counts consecutive DS output samples with no FIFO data (so a short hold stays
    // hardware-accurate and only a long one releases), `slewCount` is the number of
    // output samples still to blend after a reset/recovery, and `lastOut` is the
    // last value emitted to the DAC so a restart can be slewed instead of stepped
    // (which would be an audible click).
    u32 underrunCount;
    u32 slewCount;
    s16 lastOut;
} gbas_direct_channel7_t;

// The GBA Direct Sound FIFO is 8 words (32 bytes) deep: the hardware sound DMA
// bursts 4 words whenever at least 4 words are free, so the port must keep the
// same shallow resident depth. A deeper software FIFO makes the emulated DMA
// prefetch run ahead of the guest's mixer/driver and consume not-yet-written
// samples, which shows up as popping/clicking in streaming direct-sound titles.
#define GBAS_DIRECT_FIFO_SIZE 8
#define GBAS_DIRECT_FIFO_MASK (GBAS_DIRECT_FIFO_SIZE - 1)

typedef struct
{
    u32 fifo[GBAS_DIRECT_FIFO_SIZE];
    volatile u16 readOffset;
    volatile u16 writeOffset;

    volatile bool dmaRequest;
    // Set by the ARM9 when a SOUNDCNT_H reset strobe clears this channel's FIFO;
    // the ARM7 starts a fresh FIFO word on the next timer overflow (it holds the
    // last DAC sample across the reset, so no click is inserted).
    volatile bool fifoReset;
} gbas_direct_channel_t __attribute__((aligned(4)));

typedef struct
{
    u8 soundCntX; // only enable flags
    u8 masterEnable;
    gbas_direct_channel_t directChannels[2];
} gbas_shared_t;
