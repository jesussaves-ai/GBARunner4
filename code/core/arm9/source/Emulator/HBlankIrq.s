.section ".itcm", "ax"
.altmacro

#include "AsmMacros.inc"
#include "GbaIoRegOffsets.h"

.extern dma_state

/// @brief Performs hblank irq tasks for the emulator.
///        - Emulates HDMA
///        - Latches the guest's BG2 affine matrix for the next scanline
/// @param r0-r12 Preserved
/// @param r13 Trashed.
/// @param lr Return address.
arm_func emu_hblankIrq
    // Don't tigger hblank irq on scanlines beyond the gba screen
    mov r13, #0x04000000
    ldrh lr, [r13, #6]
    cmp lr, #260
    sublo r13, lr, #160
    rsblos r13, r13, #31
    bichs r4, r4, #2 // HBLANK IRQ

    // Install the DTCM IRQ stack once for the whole handler. r13 is trashed by
    // the prologue above (the DS I/O base and the scanline test both write it),
    // so neither the HDMA path nor the affine commit below may push onto it.
    ldr sp,= dtcmIrqStackEnd

    // Only emulate HDMA and commit the affine matrix on the visible gba lines.
    cmp lr, #160
        bge emu_hblankIrqReturn

    // r0-r3/r12 are preserved across both the DMA transfers and the affine
    // commit by a single push/pop pair.
    push {r0-r3,r12}

    // Emulate HDMA FIRST. The hblank DMA stream is what programs the next
    // scanline's parameters (the BG2 affine matrix and/or the blend/window
    // registers). The DS hblank window is only ~42us, so it must not be
    // delayed: latching the affine matrix (or draining the write buffer)
    // before the transfers can push them past the scanline edge and produces
    // flickering horizontal bands.
    //
    // The conditional branch below is taken (skipping the transfers) only on
    // scanlines >= 160, which have already returned above. It is replaced by
    // an unconditional branch to the affine commit when no hblank dma is in
    // use, and the transfer instructions are replaced by nops.
    //
    // The cmp flags above must survive into the patchable instruction below.
.global emu_hblankDmaSkipInstruction
emu_hblankDmaSkipInstruction:
    bge emu_hblankCommitAffine
#ifndef GBAR3_TEST
.global emu_hblankDmaJumpInstructions
emu_hblankDmaJumpInstructions:
    bl dma_dma0Transfer
    bl dma_dma1Transfer
    bl dma_dma2Transfer
    bl dma_dma3Transfer
#endif

    // Commit the guest's BG2 affine matrix (PA/PB/PC/PD) from the shadow to the
    // 2D engine AFTER HDMA. The per-line parameter update reaches the shadow
    // through the generic I/O store path (the affine registers fall back to the
    // shadowed store once they are not explicitly handled), so re-asserting the
    // complete, current matrix here lets the engine latch a consistent matrix at
    // the scanline boundary. Committing *before* the transfers would instead push
    // the previous scanline's (or a stale) matrix to the engine and then have it
    // overwritten mid-latch by HDMA, which shows up as an affine flash/tear.
    //
    // BG2X/BG2Y are deliberately NOT rewritten: the engine latches the reference
    // point at vblank and accumulates it by PB/PD every scanline, so overwriting
    // it here would freeze that scroll. Only an explicit guest write moves it.
emu_hblankCommitAffine:
    ldr r0, = emu_ioRegisters
    ldr r1, = 0x04000020
    ldr r2, [r0, #GBA_REG_OFFS_BG2PA]
    str r2, [r1]        // BG2PA + BG2PB
    ldr r2, [r0, #GBA_REG_OFFS_BG2PC]
    str r2, [r1, #4]    // BG2PC + BG2PD
    mcr p15, 0, r2, c7, c10, 4 // drain write buffer so the engine sees them
    pop {r0-r3,r12}
    b emu_hblankIrqReturn
.end
