.section ".itcm", "ax"
.altmacro

#include "AsmMacros.inc"

/// @brief Applies the custom turbo mapping to an active-low key input value.
/// @param r9 Raw key input (1 = released, 0 = pressed).
/// @return r9 Key input with held X/Y mapped to turbo A/B.
/// @note Clobbers r11 and r12.
arm_func emu_mapKeyInput
    mov r12, r9
    mvn r12, r12 // held buttons now read as 1
    and r12, r12, #0xC00 // X = bit 10, Y = bit 11
    mov r12, r12, lsr #10 // X/Y -> A/B bits
    orr r9, r9, #0xFC00 // GBA-unused high bits always read as released

    ldr r11,= emu_inputTurboPhase
    ldr r11, [r11]
    tst r11, #1
    bxne lr // turbo inactive on this frame

    bic r9, r9, r12 // only held X/Y force turbo A/B low
    bx lr

arm_func emu_vblankIrq
    // Advance the turbo A/B phase once per frame (DS X/Y map to A/B).
    ldr r13,= emu_inputTurboPhase
    ldr lr, [r13]
    eor lr, lr, #1
    str lr, [r13]

    // For center and mask display capture has to be enabled every frame
    // and the buffers need to be swapped
jumpToCaptureUpdate:
    nop

updateDisplayCaptureVramC:
    ldr lr,= 0x84808036
    mov r13, #0x04000000
    strh lr, [r13, #0x66]! // REG_DISPCAPCNT
    mov lr, lr, lsr #16
    add r13, r13, #(0x242 - 0x66)
    strh lr, [r13]
    ldr r13, jumpToUpdateDisplayCaptureVramDInstruction
    b checkSaveWrite

updateDisplayCaptureVramD:
    ldr lr,= 0x80848037
    mov r13, #0x04000000
    strh lr, [r13, #0x66]! // REG_DISPCAPCNT
    mov lr, lr, lsr #16
    add r13, r13, #(0x242 - 0x66)
    strh lr, [r13], -r13 // r13 becomes 0

checkSaveWrite:
    str r13, jumpToCaptureUpdate

    // This is replaced by a nop when no vblank dma is in use
.global emu_vblankDmaJumpInstruction
emu_vblankDmaJumpInstruction:
    b vblankDma

    // This is replaced by a nop when save needs to be checked
.global emu_vblankIrqSkipSaveCheckInstruction
emu_vblankIrqSkipSaveCheckInstruction:
    b emu_vblankIrqReturn
#ifndef GBAR3_TEST
    ldr r13,= gGbaSaveShared
    mcr p15, 0, r13, c7, c6, 1 // invalidate range
    ldrb lr, [r13]
    cmp lr, #3 // GBA_SAVE_STATE_WRITE
    bne emu_vblankIrqReturn

    ldr sp,= dtcmIrqStackEnd
    push {r0-r3,r12}
    bl sav_writeSaveToFile
    pop {r0-r3,r12}
    b emu_vblankIrqReturn
#endif

jumpToUpdateDisplayCaptureVramDInstruction:
    add pc, pc, #(updateDisplayCaptureVramD - jumpToCaptureUpdate - 8)

vblankDma:
    ldr sp,= dtcmIrqStackEnd
    push {r0-r3,r12}
#ifndef GBAR3_TEST
    // These are replaced by nops when not active
.global emu_vblankDmaJumpInstructions
emu_vblankDmaJumpInstructions:
    bl dma_dma0Transfer
    bl dma_dma1Transfer
    bl dma_dma2Transfer
    bl dma_dma3Transfer
#endif
    pop {r0-r3,r12}
    b emu_vblankIrqSkipSaveCheckInstruction

.pool
.end
