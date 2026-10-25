#include "common.h"
#include "SdCache/SdCache.h"
#include "JitCommon.h"
#include "MemoryEmulator/MemoryLoadStore.h"
#include "MemoryEmulator/RomDefs.h"
#include "JitArm.h"

// Known unsupported ARM forms (each currently traps via armJitNotImplemented()
// and therefore needs a new trap encoding *and* a runtime handler before it can
// be completed; changing these safely requires the GBA test-ROM harness):
//   * ALU{S} pc, Rn, Rm with a *register-shifted register* operand (bit4=1).
//     The imm-shift form is folded to an undefined opcode and serviced by
//     vm_armUndefinedAluSPCImm (.itcm); the reg-shift form has no encoding.
//   * Post-indexed `LDR pc, [Rn], #imm` (P=0). Only pre-indexed is folded.
//   * ALU ops on pc other than SUB/ADD/MOV in jit_handleArmUndefined (AND, EOR,
//     RSB, ADC, SBC, RSC, ORR, BIC, MVN); the rare S=1 exception-return forms
//     of these need CPSR<-SPSR handling that the handler does not do.
//   * Unrecognised undefined Thumb encodings (jit_handleThumbUndefined default).

[[gnu::noreturn]]
static void armJitNotImplemented()
{
    asm volatile ("bkpt #0");
    __builtin_unreachable();
}

[[gnu::noinline]]
static void armEncodeAluPc(u32* ptr, u32 instruction, u32 base)
{
    *ptr = base
        | (instruction & 0xF0000000) // cond
        | ((instruction & 0x01E00000) >> 4) // op
        | ((instruction & 0x00100000) << 1) // S
        | ((instruction & 0x000F0000) >> 16) // Rn
        | ((instruction & 0x00000FFF) << 5); // op2
}

/// @brief Applies an ARM data-processing shift operand (ARM ARM "Shifts").
/// @param shiftType 0..3 = LSL, LSR, ASR, ROR.
/// @param shiftAmount The 5-bit field. The special #0 encodings mean: LSR#0 is
///        a shift by 32 (result 0), ASR#0 is a shift by 32 (sign fill), and
///        ROR#0 is RRX (rotate right by one bit through the carry flag).
/// @param carry The current CPSR carry bit (bit 29), consumed by RRX.
static u32 armGetShiftedOperand(u32 value, u32 shiftType, u32 shiftAmount, u32 carry)
{
    switch (shiftType)
    {
        case 0: // LSL
            return shiftAmount == 0 ? value : (value << shiftAmount);
        case 1: // LSR
            return shiftAmount == 0 ? 0 : (value >> shiftAmount);
        case 2: // ASR
            return shiftAmount == 0 ? (u32)((int)value >> 31)
                                    : (u32)((int)value >> shiftAmount);
        default: // ROR (#0 = RRX)
            if (shiftAmount == 0)
                return (value >> 1) | (carry << 31);
            return (value >> shiftAmount) | (value << (32 - shiftAmount));
    }
}

bool jit_processArmInstruction(u32* ptr)
{
    u32 instruction = *ptr;
    if ((instruction & 0x0E000000) == 0x0A000000)
    {
        // B and BL imm
        *ptr = (instruction & ~0x0E000000) | 0x0C000000;
        if ((instruction >> 28) == 0xE)
        {
            return false;
        }
    }
    else if ((instruction & 0x0FBF0FFF) == 0x010F0000)
    {
        // MRS
        *ptr = 0x01A00090 | (instruction & 0xF0400000) | ((instruction & 0x0000F000) >> 12);
    }
    else if ((instruction & 0x0FB0FFF0) == 0x0120F000)
    {
        // MSR reg
        *ptr = 0x1800090 | (instruction & 0xF04F000F) | ((instruction & 0x02000000) >> 5);
    }
    else if ((instruction & 0x0FB0F000) == 0x0320F000)
    {
        // MSR imm
        *ptr = 0x1900090
            | (instruction & 0xF04F000F)
            | ((instruction & 0x00000FF0) << 4)
            | ((instruction & 0x02000000) >> 5);
    }
    else if ((instruction & 0x0FFFFFF0) == 0x012FFF10)
    {
        // BX
        *ptr = 0x01B00090 | (instruction & 0xF000000F);
        if ((instruction >> 28) == 0xE)
        {
            return false;
        }
    }
    else if ((instruction & 0x0E108000) == 0x08108000)
    {
        // LDM pc
        *ptr = 0x06400010
            | (instruction & 0xF1A00000) // cond, P, U, W
            | ((instruction & 0x000F0000) >> 16) // Rn
            | ((instruction & 0x7FFF) << 5); // rlist
        if ((instruction >> 28) == 0xE)
        {
            return false;
        }
    }
    else if ((instruction & 0x0E300000) == 0x08300000)
    {
        // LDM with writeback and base register first reg in rlist
        u32 baseReg = (instruction >> 16) & 0xF;
        if ((instruction & (1 << baseReg)) && !(instruction & ((1 << baseReg) - 1)))
        {
            // disable writeback
            *ptr &= ~(1 << 21);
        }
    }
    else if ((instruction & 0x0C50F000) == 0x0410F000)
    {
        // LDR pc
        if (!(instruction & 0x01000000))
        {
            // post
            armJitNotImplemented();
        }
        else
        {
            *ptr = 0x0E800010
                | (instruction & 0xF0200000) // cond, W
                | ((instruction & 0x000F0000) >> 16) // Rd
                | ((instruction & 0xF) << 5) // op2
                | ((instruction & 0xFF0) << 8) // op2
                | ((instruction & 0x01800000) >> 14) // P, U
                | ((instruction & 0x02000000) >> 3); // I
        }
        if ((instruction >> 28) == 0xE)
        {
            return false;
        }
    }
    else if ((instruction & 0x0E00F010) == 0x0000F000)
    {
        if ((instruction & 0x0FFFFFF0) == 0x01B0F000 && (instruction & 0xF) != 15)
        {
            // MOVS pc, Rm
            // encode it as undefined subs pc, Rm, #0
            *ptr = 0x0E640000
                | (instruction & 0xF0000000) // cond
                | (instruction & 0x0000000F); // Rm
        }
        else
        {
            // ALU{S} pc, Rn, Rm (imm shift)
            armEncodeAluPc(ptr, instruction, 0x0E000000);
        }
        if ((instruction >> 28) == 0xE)
        {
            return false;
        }
    }
    else if ((instruction & 0x0E00F010) == 0x0000F010)
    {
        // ALU{S} pc, Rn, Rm (reg shift)
        armJitNotImplemented();
        if ((instruction >> 28) == 0xE)
        {
            return false;
        }
    }
    else if ((instruction & 0x0E00F000) == 0x0200F000)
    {
        // ALU{S} pc, Rn, #imm
        armEncodeAluPc(ptr, instruction, 0x0E400000);
        if ((instruction >> 28) == 0xE)
        {
            return false;
        }
    }
    else if ((instruction & 0x0F000000) == 0x0F000000)
    {
        // SWI
        u32 swiOp = instruction & 0xFFFFFF;
        if (swiOp == 0 || swiOp == 0x260000)
        {
            if ((instruction >> 28) == 0xE)
            {
                return false;
            }
        }
    }

    return true;
}

void jit_processArmBlock(u32* ptr)
{
    // logAddress(0xA);
    // logAddress((u32)ptr);
    void* const blockStart = jit_findBlockStart(ptr);
    void* blockEnd = jit_findBlockEnd(ptr);
    u8* jitBits = jit_getJitBits(ptr);
    do
    {
        u32 bitIdx = ((u32)ptr & 0xF) >> 1;
        u32 bitMask = 3 << bitIdx;
        if (*jitBits & bitMask)
        {
            // stop because this instruction was already processed
            break;
        }
        *jitBits |= bitMask;
        if (bitIdx == 6)
            jitBits++;
        if (!jit_processArmInstruction(ptr))
        {
            break;
        }
        u32 instruction = *ptr;
        if ((instruction & 0x0C5F0000) == 0x041F0000)
        {
            // LDR Rd, [pc, ...]
            if (!(instruction & 0x02000000))
            {
                // LDR Rd, [pc, #imm]
                int offset = instruction & 0xFFF;
                if (!(instruction & 0x00800000))
                    offset = -offset;                
                u32 targetAddress = (u32)ptr + 8 + offset;
                if (targetAddress >= (u32)blockStart && targetAddress < (u32)blockEnd)
                {
                    if (targetAddress > (u32)ptr)
                        blockEnd = (void*)targetAddress;
                    // safe pool address needs no patching
                    continue;
                }
            }
        }
    } while ((u32)++ptr < (u32)blockEnd);
}

[[gnu::section(".itcm")]]
__attribute__((optimize("Os"))) u32* jit_handleArmUndefined(u32 instruction, u32* instructionPtr, u32* registers, u32 cpsr)
{
    // if ((instruction & 0x0F000000) == 0x0C000000)
    // {
    //     // b cond
    //     u32 originalInstruction = *(u32*)(((u32)instructionPtr & ~0x01000000) + 0x00400000);
    //     u32 condition = originalInstruction >> 28;
    //     bool conditionPass = jit_conditionPass(cpsr, condition);
    //     u32 branchDestination = (u32)instructionPtr + 8 + ((int)(instruction << 8) >> 6);
    //     bool putBack;
    //     u32 address;
    //     if (conditionPass)
    //     {
    //         address = branchDestination;
    //         jit_ensureBlockJitted((void*)address);
    //         putBack = jit_isBlockJitted(instructionPtr + 1);
    //     }
    //     else
    //     {
    //         address = (u32)(instructionPtr + 1);
    //         jit_ensureBlockJitted((void*)address);
    //         putBack = jit_isBlockJitted((void*)branchDestination);
    //     }
    //     if (putBack)
    //     {
    //         *instructionPtr = originalInstruction;
    //         dc_drainWriteBuffer();
    //         ic_invalidateAll();
    //     }
    //     return (u32*)address;
    // }
    if ((instruction & 0x0FA00010) == 0x0E000000)
    {
        // ALU{S} pc, Rn, Rm/#imm
        u32 op = (instruction >> 17) & 0xF;
        u32 rn = registers[instruction & 0xF];
        u32 op2;
        if (!(instruction & 0x00400000))
        {
            // ALU{S} pc, Rn, Rm (imm shift)
            u32 rm = registers[(instruction >> 5) & 0xF];
            u32 shiftType = (instruction >> 10) & 0x3;
            u32 shiftAmount = (instruction >> 12) & 0x1F;
            op2 = armGetShiftedOperand(rm, shiftType, shiftAmount, (cpsr >> 29) & 1);
        }
        else
        {
            // ALU{S} pc, Rn, #imm
            u32 imm8 = (instruction >> 5) & 0xFF;
            u32 ror = ((instruction >> 13) & 0xF) << 1;
            op2 = (imm8 >> ror) | (imm8 << (32 - ror));
        }
        u32 branchTarget;
        switch (op)
        {
            case 2: // sub
                branchTarget = rn - op2;
                break;
            case 4: // add
                branchTarget = rn + op2;
                break;
            case 0xD: // mov
                branchTarget = op2;
                break;
            default:
                armJitNotImplemented();
                break;
        }
        branchTarget &= ~3;
        jit_ensureBlockJitted((void*)branchTarget);
        return (u32*)branchTarget;
    }
    else if ((instruction & 0x0FF00010) == 0x06E00010)
    {
        // LDMIA Rn!, {...,pc}
        u32 rn = instruction & 0xF;
        u32* src = (u32*)registers[rn];
        u32 align = (u32)src & 3;
        src = (u32*)((u32)src & ~3);
        for (u32 i = 0; i < 15; i++)
        {
            if (instruction & (0x20 << i))
            {
                registers[i] = *src++;
            }
        }
        u32 branchDestination = *src++;
        branchDestination &= ~3;
        registers[rn] = (u32)src | align;
        jit_ensureBlockJitted((void*)branchDestination);
        return (u32*)branchDestination;
    }
    else if ((instruction & 0x0FF00C10) == 0x0EC00410)
    {
        // ldr pc, [Rn, Rm, shift]
        u32 rn = registers[instruction & 0xF];
        u32 rm = registers[(instruction >> 5) & 0xF];
        u32 shiftType = (instruction >> 13) & 0x3;
        u32 shiftAmount = (instruction >> 15) & 0x1F;
        u32 op2 = armGetShiftedOperand(rm, shiftType, shiftAmount, (cpsr >> 29) & 1);
        u32 address = (rn + op2) & ~3;
        u32 branchDestination;
        if (address >= ROM_LINEAR_DS_ADDRESS && address < ROM_LINEAR_END_DS_ADDRESS)
        {
            branchDestination = *(u32*)address;
        }
        else
        {
            branchDestination = memu_load32FromC(address);
        }
        branchDestination &= ~3;
        jit_ensureBlockJitted((void*)branchDestination);
        return (u32*)branchDestination;
    }
    armJitNotImplemented();
    return instructionPtr + 1;
}
