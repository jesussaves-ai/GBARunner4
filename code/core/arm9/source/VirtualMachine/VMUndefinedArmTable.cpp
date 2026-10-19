#include "common.h"
#include "VMUndefinedArmTable.h"

#pragma GCC optimize ("Oz")

extern const void* vm_armUndefinedMsrRegSpsrRmTable[16];
extern const void* vm_armUndefinedMsrRegCpsrRmTable[16];
extern const void* vm_armUndefinedMsrImmTable[16];
extern const void* vm_armUndefinedMrsSpsrRmTable[16];
extern const void* vm_armUndefinedMrsCpsrRmTable[16];
extern const void* jit_armUndefinedBxRmTable[16];
extern const void* jit_armUndefinedBTable[16];
extern const void* jit_armUndefinedBLTable[16];
extern const void* jit_armUndefinedLdrPcImmRnTable[16];
extern const void* vm_armUndefinedAluSPCImmRnTable[16];

// Zero-initialized in .dtcm; only non-zero entries are programmed at startup.
DTCM_DATA u8 vm_undefinedArmTable[1024] = {};

void vm_initializeUndefinedArmTable()
{
    const u8 msrRegCpsr = -(int)vm_armUndefinedMsrRegCpsrRmTable >> 6;
    const u8 msrRegSpsr = -(int)vm_armUndefinedMsrRegSpsrRmTable >> 6;
    const u8 msrImm = -(int)vm_armUndefinedMsrImmTable >> 6;
    const u8 mrsCpsr = -(int)vm_armUndefinedMrsCpsrRmTable >> 6;
    const u8 mrsSpsr = -(int)vm_armUndefinedMrsSpsrRmTable >> 6;
    const u8 aluPcImm = -(int)vm_armUndefinedAluSPCImmRnTable >> 6;
#ifndef GBAR3_TEST
    const u8 bx = -(int)jit_armUndefinedBxRmTable >> 6;
    const u8 b = -(int)jit_armUndefinedBTable >> 6;
    const u8 bl = -(int)jit_armUndefinedBLTable >> 6;
    const u8 ldrPcImm = -(int)jit_armUndefinedLdrPcImmRnTable >> 6;
#endif

    for (u32 base = 0; base < sizeof(vm_undefinedArmTable); base += 0x100)
    {
        vm_undefinedArmTable[base + 0x18] = msrRegCpsr;
        vm_undefinedArmTable[base + 0x1C] = msrRegSpsr;
        vm_undefinedArmTable[base + 0x19] = msrImm;
        vm_undefinedArmTable[base + 0x1D] = msrImm;
        vm_undefinedArmTable[base + 0x1A] = mrsCpsr;
        vm_undefinedArmTable[base + 0x1E] = mrsSpsr;
#ifndef GBAR3_TEST
        vm_undefinedArmTable[base + 0x1B] = bx;
        // Prefix 6: d=0 selects B, d=1 selects BL.
        for (u32 i = 0; i < 16; ++i)
        {
            vm_undefinedArmTable[base + 0xC0 + i] = b;
            vm_undefinedArmTable[base + 0xD0 + i] = bl;
        }
#endif
    }
#ifndef GBAR3_TEST
    vm_undefinedArmTable[0x2E8] = ldrPcImm;
#endif
    vm_undefinedArmTable[0x0E6] = aluPcImm;
    vm_undefinedArmTable[0x0E7] = aluPcImm;
    vm_undefinedArmTable[0x1E6] = aluPcImm;
    vm_undefinedArmTable[0x1E7] = aluPcImm;
}
