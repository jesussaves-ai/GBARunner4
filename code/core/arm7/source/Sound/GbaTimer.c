#include "common.h"
#include "GbaTimer.h"

#define TICKS_PER_UPDATE    512
#define TICK_SHIFT          1

static const u32 sPrescaleShiftedTicksPerUpdate[4] =
{
    TICKS_PER_UPDATE << TICK_SHIFT,
    (TICKS_PER_UPDATE << TICK_SHIFT) >> 6,
    (TICKS_PER_UPDATE << TICK_SHIFT) >> 8,
    (TICKS_PER_UPDATE << TICK_SHIFT) >> 10
};

void gbat_initTimer(gbat_t* timer)
{
    timer->isStarted = 0;
    timer->curNrOverflows = 0;
    timer->reload = 0;
    timer->control = 0;
    timer->counter = 0;
}

u32 gbat_updateTimer(gbat_t* timer, u32 cascadeOverflows)
{
    u32 control = timer->control;
    if (!(control & GBAT_CONTROL_ENABLED))
    {
        timer->isStarted = false;
        return 0;
    }

    if (!timer->isStarted)
    {
        timer->counter = timer->reload << TICK_SHIFT;
        timer->isStarted = true;
        return 0;
    }
    else
    {
        u32 ticks;
        if (control & GBAT_CONTROL_SLAVE)
        {
            // Cascade mode: increment once per Timer 0 overflow. The prescaler is
            // ignored by hardware, so feed the previous timer's overflow count
            // (already scaled into this counter's *2 domain) instead of cycle
            // ticks. Without this a Direct Sound channel assigned to a cascaded
            // Timer 1 consumed no FIFO words (silence).
            ticks = cascadeOverflows << TICK_SHIFT;
        }
        else
        {
            ticks = sPrescaleShiftedTicksPerUpdate[control & GBAT_CONTROL_PRESCALE_MASK];
        }

        u32 counter = timer->counter + ticks;
        u32 reload = (timer->reload - 0x10000) << TICK_SHIFT;
        u32 overflows = 0;
        while (counter >= (0x10000 << TICK_SHIFT))
        {
            counter += reload;
            ++overflows;
        }
        timer->counter = counter;
        return overflows;
    }
}
