.section ".itcm", "ax"
.altmacro

#include "AsmMacros.inc"

arm_func emu_ioFallbackLoad32
    ldr r11,= (emu_ioRegisters - 0x04000000)
    ldr r9, [r11, r8]
    bx lr

arm_func emu_ioFallbackLoad16
    ldr r11,= (emu_ioRegisters - 0x04000000)
    ldrh r9, [r11, r8]
    bx lr

arm_func emu_ioFallbackStore16
    // Registers handled here (BG/OBJ scroll offsets, the BG2/BG3 affine matrix
    // and reference points, the window dimensions, mosaic, ...) are read back
    // from the shadow by emu_ioFallbackLoad16, by the mode-change re-apply in
    // RegDispCnt.c and by the per-line affine commit in HBlankIrq.s. Update the
    // shadow in lock-step with the hardware, otherwise those readers see a
    // stale value (a freshly zeroed shadow commits a zero affine matrix every
    // visible scanline, which tears/flashes the affine layer).
    ldr r11,= (emu_ioRegisters - 0x04000000)
    strh r9, [r8]
    strh r9, [r11, r8]
    bx lr

arm_func emu_ioStore16
    ldr r11,= (emu_ioRegisters - 0x04000000)
    strh r9, [r11, r8]
    bx lr

arm_func emu_ioFallbackStore32
    ldr r11,= (emu_ioRegisters - 0x04000000)
    str r9, [r8]
    str r9, [r11, r8]
    bx lr

arm_func emu_ioStore32
    ldr r11,= (emu_ioRegisters - 0x04000000)
    str r9, [r11, r8]
    bx lr