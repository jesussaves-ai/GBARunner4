#include "common.h"
#include <string.h>
#include "SdCache/SdCache.h"
#include "cp15.h"
#include "MemoryEmulator/RomDefs.h"
#include "VirtualMachine/VMIrq.h"
#include "JitArm.h"
#include "JitThumb.h"
#include "JitCommon.h"

[[gnu::section(".ewram.bss")]]
jit_state_t gJitState;

/// @brief Whether the JIT patcher is actively processing blocks. jit_disable()
///        sets every processed bit so jit_ensureBlockJitted() becomes a no-op;
///        jit_invalidateCodeRange() must not clear those bits in that state or
///        it would silently re-enable RAM patching for a DMA'd range.
[[gnu::section(".ewram.bss")]]
bool gJitPatcherActive;

[[gnu::optimize("Oz")]]
u32 jit_getJitBitsOffset(const void* ptr)
{
    u32 jitBitsOffset;
    u32 offset;

    if ((u32)ptr >= ROM_LINEAR_DS_ADDRESS && (u32)ptr < ROM_LINEAR_END_DS_ADDRESS)
    {
        // static rom region
        jitBitsOffset = offsetof(jit_state_t, staticRomJitBits);
        offset = (u32)ptr - ROM_LINEAR_DS_ADDRESS;
    }
    else if ((u32)ptr >= 0x03000000 && (u32)ptr < 0x04000000)
    {
        // IWRAM
        jitBitsOffset = offsetof(jit_state_t, iWramJitBits);
        offset = (u32)ptr & 0x7FFF;
    }
    else if ((u32)ptr >= (u32)sdc_cache && (u32)ptr < (u32)sdc_cache[SDC_BLOCK_COUNT])
    {
        // sd cache
        jitBitsOffset = offsetof(jit_state_t, dynamicRomJitBits);
        offset = (u32)ptr - (u32)sdc_cache;
    }
    else if ((u32)ptr >= 0x02000000 && (u32)ptr < 0x02040000)
    {
        // EWRAM
        jitBitsOffset = offsetof(jit_state_t, eWramJitBits);
        offset = (u32)ptr - 0x02000000;
    }
    else if ((u32)ptr >= 0x06000000 && (u32)ptr < 0x06018000)
    {
        // VRAM
        jitBitsOffset = offsetof(jit_state_t, vramJitBits);
        offset = (u32)ptr - 0x06000000;
    }
    else
    {
        jitBitsOffset = offsetof(jit_state_t, dummyJitBits);
        offset = 0;
    }

    return jitBitsOffset + (offset / 2 / 8);
}

void* jit_findBlockStart(const void* ptr)
{
    if ((u32)ptr >= (u32)sdc_cache && (u32)ptr < (u32)sdc_cache[SDC_BLOCK_COUNT])
    {
        // sd cache
        return (void*)((u32)ptr & ~SDC_BLOCK_MASK);
    }
    else if ((u32)ptr >= ROM_LINEAR_DS_ADDRESS && (u32)ptr < ROM_LINEAR_END_DS_ADDRESS)
    {
        // static rom region
        return (void*)ROM_LINEAR_DS_ADDRESS;
    }
    // no significant block boundary
    return (void*)0;
}

void* jit_findBlockEnd(const void* ptr)
{
    if ((u32)ptr >= (u32)sdc_cache && (u32)ptr < (u32)sdc_cache[SDC_BLOCK_COUNT])
    {
        // sd cache
        return (void*)(((u32)ptr & ~SDC_BLOCK_MASK) + SDC_BLOCK_SIZE);
    }
    else if ((u32)ptr >= ROM_LINEAR_DS_ADDRESS && (u32)ptr < ROM_LINEAR_END_DS_ADDRESS)
    {
        // static rom region
        return (void*)ROM_LINEAR_END_DS_ADDRESS;
    }
    // no significant block boundary
    return (void*)0xFFFFFFFF;
}

bool jit_isBlockJitted(void* ptr)
{
    if ((u32)ptr >= ROM_LINEAR_GBA_ADDRESS)
    {
        ptr = (void*)((u32)ptr - ROM_LINEAR_GBA_ADDRESS + ROM_LINEAR_DS_ADDRESS);
    }

    const u8* const jitBits = jit_getJitBits(ptr);
    u32 bitIdx = ((u32)ptr & 0xF) >> 1;
    return (*jitBits >> bitIdx) & 1;
}

void jit_ensureBlockJitted(void* ptr)
{
    if ((u32)ptr >= ROM_LINEAR_GBA_ADDRESS)
    {
        ptr = (void*)((u32)ptr - ROM_LINEAR_GBA_ADDRESS + ROM_LINEAR_DS_ADDRESS);
    }

    const u8* const jitBits = jit_getJitBits(ptr);
    u32 bitIdx = ((u32)ptr & 0xF) >> 1;
    if ((*jitBits >> bitIdx) & 1)
        return;
    if ((u32)ptr & 1)
    {
        jit_processThumbBlock((u16*)((u32)ptr & ~1));
    }
    else
    {
        jit_processArmBlock((u32*)ptr);
    }
    dc_drainWriteBuffer();
    ic_invalidateAll();
}

/// @brief Bit-table chunk is 16 bytes (one processed byte / one aux halfword).
#define JIT_CHUNK_SHIFT     4
#define JIT_CHUNK_BYTES     (1 << JIT_CHUNK_SHIFT)

void jit_invalidateCodeRange(u32 gbaAddress, u32 byteCount)
{
    if (!gJitPatcherActive)
    {
        // With the JIT disabled every processed bit reads as set; clearing them
        // here would re-enable RAM patching for the DMA'd range.
        return;
    }

    u8* procBits;
    u16* auxBits;
    u32 offset;
    u32 chunkCount;

    if (gbaAddress >= 0x03000000 && gbaAddress < 0x04000000)
    {
        // IWRAM (mirrored over the 16 MB region): 32 KB, indexed with & 0x7FFF.
        procBits = (u8*)gJitState.iWramJitBits;
        auxBits = (u16*)gJitState.iWramJitAuxBits;
        offset = gbaAddress & 0x7FFF;
        chunkCount = (32 * 1024) / JIT_CHUNK_BYTES;
    }
    else if (gbaAddress >= 0x02000000 && gbaAddress < 0x03000000)
    {
        // EWRAM: 256 KB, indexed with & 0x3FFFF.
        procBits = (u8*)gJitState.eWramJitBits;
        auxBits = (u16*)gJitState.eWramJitAuxBits;
        offset = gbaAddress & 0x3FFFF;
        chunkCount = (256 * 1024) / JIT_CHUNK_BYTES;
    }
    else
    {
        return;
    }

    if (byteCount == 0)
        return;

    u32 firstChunk = offset >> JIT_CHUNK_SHIFT;
    u32 lastChunk = (offset + byteCount - 1) >> JIT_CHUNK_SHIFT;
    if (lastChunk >= chunkCount)
        lastChunk = chunkCount - 1;

    // Scan and clear whole words (a word covers 4 chunks = 64 bytes). Clearing a
    // few extra chunks at the range edges is harmless: it only forces those
    // bytes to be re-jitted, and they were not overwritten by this DMA.
    u32* procWords = (u32*)procBits;
    u32* auxWords = (u32*)auxBits;
    u32 firstWord = firstChunk >> 2;
    u32 lastWord = lastChunk >> 2;

    bool anySet = false;
    for (u32 i = firstWord; i <= lastWord; i++)
    {
        if (procWords[i] != 0)
        {
            procWords[i] = 0;
            anySet = true;
        }
        if (auxWords[i] != 0)
        {
            auxWords[i] = 0;
        }
    }

    if (!anySet)
    {
        return; // pure data DMA: the bit table was already clear
    }

    // The range held JIT-processed (executing) code. Clean the freshly written
    // bytes to memory and drop stale instruction-cache lines so the new code is
    // fetched and re-jitted instead of the previous contents running.
    u32 dsAddress;
    u32 regionEnd;
    if (gbaAddress >= 0x03000000)
    {
        dsAddress = 0x03000000u | (gbaAddress & 0x7FFFu);
        regionEnd = 0x03008000u;
    }
    else
    {
        dsAddress = 0x02000000u | (gbaAddress & 0x3FFFFu);
        regionEnd = 0x02040000u;
    }
    if (dsAddress + byteCount > regionEnd)
        byteCount = regionEnd - dsAddress;
    if (byteCount == 0)
        return;

    u32 aligned = dsAddress & ~0x1Fu;
    dc_flushRange((void*)aligned, byteCount + (dsAddress - aligned));
    dc_drainWriteBuffer();
    ic_invalidateAll();
}

void jit_init(void)
{
    memset(&gJitState, 0, sizeof(gJitState));
    gJitState.dummyJitBits = ~0u;
    gJitPatcherActive = true;
}

void jit_disable(void)
{
    gJitPatcherActive = false;
    memset(gJitState.staticRomJitBits, 0xFF, sizeof(gJitState.staticRomJitBits));
    memset(gJitState.dynamicRomJitBits, 0xFF, sizeof(gJitState.dynamicRomJitBits));
    memset(gJitState.iWramJitBits, 0xFF, sizeof(gJitState.iWramJitBits));
    memset(gJitState.eWramJitBits, 0xFF, sizeof(gJitState.eWramJitBits));
    memset(gJitState.vramJitBits, 0xFF, sizeof(gJitState.vramJitBits));

    for (u32 i = 0; i < VM_JUMP_TO_IRQ_HANDLER_COMMON_INSTRUCTION_COUNT; i++)
    {
        vm_jumpToIrqHandler[i] = vm_jumpToIrqHandlerCommon[i];
    }

    vm_jumpToIrqHandler[2] -= 8; // fix the ldr r4, DTCM(vm_irqSavedR4)
}
