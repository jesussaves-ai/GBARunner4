.section ".itcm", "ax"
.altmacro

#include "AsmMacros.inc"
#include "GbaIoRegOffsets.h"

.extern dma_state

/// @brief Performs hblank irq tasks for the emulator.
///        - Latches the guest's BG2 affine matrix for the next scanline
///        - Emulates HDMA
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

    cmp lr, #160
        bge emu_hblankIrqReturn

    // Commit the guest's BG2 affine matrix (PA/PB/PC/PD) from the shadow to the
    // 2D engine at the scanline boundary. Per-line HBlank/HDMA parameter updates
    // reach the engine through the generic I/O store path; re-asserting the
    // matrix here (and draining the write buffer) guarantees the engine latches
    // the complete, current matrix before rendering the next line, so a per-line
    // update can never be sampled half-updated (affine shearing). This runs for
    // every visible hblank, whether the update came from a cpu store or HDMA.
    //
    // BG2X/BG2Y are deliberately NOT rewritten: the engine latches the reference
    // point at vblank and accumulates it by PB/PD every scanline, so overwriting
    // it here would freeze that scroll. Only an explicit guest write moves it.
    push {r0-r3,r12}
    ldr r0, = emu_ioRegisters
    ldr r1, = 0x04000020
    ldr r2, [r0, #GBA_REG_OFFS_BG2PA]
    str r2, [r1]        // BG2PA + BG2PB
    ldr r2, [r0, #GBA_REG_OFFS_BG2PC]
    str r2, [r1, #4]    // BG2PC + BG2PD
    mcr p15, 0, r2, c7, c10, 4 // drain write buffer so the engine sees them
    pop {r0-r3,r12}

    // The cmp flags above must survive into the patchable instruction below.
    // This is replaced by a b instruction when no hblank dma is in use
.global emu_hblankDmaSkipInstruction
emu_hblankDmaSkipInstruction:
    bge emu_hblankIrqReturn

    ldr sp,= dtcmIrqStackEnd
    push {r0-r3,r12}
#ifndef GBAR3_TEST
    // These are replaced by nops when not active
.global emu_hblankDmaJumpInstructions
emu_hblankDmaJumpInstructions:
    bl dma_dma0Transfer
    bl dma_dma1Transfer
    bl dma_dma2Transfer
    bl dma_dma3Transfer
#endif
    pop {r0-r3,r12}
    b emu_hblankIrqReturn
