.text
.arm

#include "AsmMacros.inc"

arm_func mpu_setRegionInstructionCacheEnable
    mov r2, #1
    mov r0, r2, lsl r0
    cmp r1, #0
    mrc p15, 0, r2, c2, c0, 1
    biceq r2, r2, r0
    orrne r2, r2, r0
    mcr p15, 0, r2, c2, c0, 1
    bx lr

arm_func mpu_setRegionDataCacheEnable
    mov r2, #1
    mov r0, r2, lsl r0
    cmp r1, #0
    mrc p15, 0, r2, c2, c0, 0
    biceq r2, r2, r0
    orrne r2, r2, r0
    mcr p15, 0, r2, c2, c0, 0
    bx lr

arm_func mpu_setRegionDataBufferability
    mov r2, #1
    mov r0, r2, lsl r0
    cmp r1, #0
    mrc p15, 0, r2, c3, c0, 0
    biceq r2, r2, r0
    orrne r2, r2, r0
    mcr p15, 0, r2, c3, c0, 0
    bx lr

arm_func mpu_setRegionBaseAndSize
    add pc, pc, r0, lsl #3
    nop
    mcr	p15, 0, r1, c6, c0, 0
    bx lr
    mcr	p15, 0, r1, c6, c1, 0
    bx lr
    mcr	p15, 0, r1, c6, c2, 0
    bx lr
    mcr	p15, 0, r1, c6, c3, 0
    bx lr
    mcr	p15, 0, r1, c6, c4, 0
    bx lr
    mcr	p15, 0, r1, c6, c5, 0
    bx lr
    mcr	p15, 0, r1, c6, c6, 0
    bx lr
    mcr	p15, 0, r1, c6, c7, 0
    bx lr
