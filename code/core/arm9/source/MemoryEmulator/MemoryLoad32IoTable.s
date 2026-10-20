.section ".dtcm", "aw"
.altmacro

#include "AsmMacros.inc"
#include "GbaIoRegOffsets.h"

.macro memu_ioRegLoad32Pointer index
    .if (\index << 2) == GBA_REG_OFFS_DISPCNT
        .short emu_ioFallbackLoad32
    .elseif (\index << 2) == GBA_REG_OFFS_DISPSTAT
        .short emu_regDispStatVCountLoad32 // REG_DISPSTAT and REG_VCOUNT
    .elseif (((\index << 2) >= GBA_REG_OFFS_BG2PA) && ((\index << 2) <= GBA_REG_OFFS_BG3Y))
        // BG2/BG3 affine matrix pairs and 32-bit reference points. Read them
        // back from the shadow so mode 7 style camera code that reloads the
        // current coordinates keeps full 28-bit precision.
        .short emu_ioFallbackLoad32
    .elseif ((\index << 2) >= GBA_REG_OFFS_BG0HOFS) && ((\index << 2) <= GBA_REG_OFFS_WIN1V)
        .short memu_load32Undefined
    .elseif (\index << 2) == GBA_REG_OFFS_MOSAIC
        .short memu_load32Undefined
    .elseif (\index << 2) == GBA_REG_OFFS_BLDY
        .short memu_load32Undefined
    .elseif (\index << 2) == GBA_REG_OFFS_SOUNDCNT_X
        .short emu_gbaSoundLoadSoundCntX
    .elseif (\index << 2) == GBA_REG_OFFS_FIFO_A
        .short memu_load32Undefined
    .elseif (\index << 2) == GBA_REG_OFFS_FIFO_B
        .short memu_load32Undefined
    .elseif (\index << 2) == GBA_REG_OFFS_DMA0SAD
        .short memu_load32Undefined
    .elseif (\index << 2) == GBA_REG_OFFS_DMA0DAD
        .short memu_load32Undefined
    @ .elseif (\index << 2) == GBA_REG_OFFS_DMA0CNT_L
    @     .short memu_load16Undefined
    .elseif (\index << 2) == GBA_REG_OFFS_DMA1SAD
        .short memu_load32Undefined
    .elseif (\index << 2) == GBA_REG_OFFS_DMA1DAD
        .short memu_load32Undefined
    @ .elseif (\index << 2) == GBA_REG_OFFS_DMA1CNT_L
    @     .short memu_load16Undefined
    .elseif (\index << 2) == GBA_REG_OFFS_DMA2SAD
        .short memu_load32Undefined
    .elseif (\index << 2) == GBA_REG_OFFS_DMA2DAD
        .short memu_load32Undefined
    @ .elseif (\index << 2) == GBA_REG_OFFS_DMA2CNT_L
    @     .short memu_load16Undefined
    .elseif (\index << 2) == GBA_REG_OFFS_DMA3SAD
        .short memu_load32Undefined
    .elseif (\index << 2) == GBA_REG_OFFS_DMA3DAD
        .short memu_load32Undefined
    @ .elseif (\index << 2) == GBA_REG_OFFS_DMA3CNT_L
    @     .short memu_load16Undefined
    .elseif ((\index << 2) > GBA_REG_OFFS_DMA3CNT) && ((\index << 2) < GBA_REG_OFFS_TM0CNT)
        .short memu_load32Undefined
    .elseif (\index << 2) == GBA_REG_OFFS_TM0CNT
        .short emu_timerCntLoad32
    .elseif (\index << 2) == GBA_REG_OFFS_TM1CNT
        .short emu_timerCntLoad32
    .elseif (\index << 2) == GBA_REG_OFFS_TM2CNT
        .short emu_timerCntLoad32
    .elseif (\index << 2) == GBA_REG_OFFS_TM3CNT
        .short emu_timerCntLoad32
    .elseif (\index << 2) == GBA_REG_OFFS_KEYINPUT
        .short emu_regKeyInputKeyControlLoad32
    .elseif ((\index << 2) > GBA_REG_OFFS_JOYSTAT) && ((\index << 2) < GBA_REG_OFFS_IE)
        .short memu_load32Undefined
    .elseif (\index << 2) == GBA_REG_OFFS_IE
        .short emu_regIeIfLoad32 // REG_IE and REG_IF
    .else
        .short emu_ioFallbackLoad32
    .endif
.endm

.global memu_load32IoTable
memu_load32IoTable:
generate memu_ioRegLoad32Pointer 0x84
