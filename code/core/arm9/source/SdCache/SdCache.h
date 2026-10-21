#pragma once
#include "SdCacheDefs.h"
#include "MemoryEmulator/HiCodeCacheMapping.h"
#include "cp15.h"
#include "VirtualMachine/VMNestedIrq.h"

/// @brief The sd cache blocks.
extern u8 sdc_cache[SDC_BLOCK_COUNT][SDC_BLOCK_SIZE];

/// @brief Maps rom blocks to their location in memory.
///        A value of 0 indicates the block is not loaded.
///        Blocks [0, ROM_LINEAR_SIZE / SDC_BLOCK_SIZE) are permanently
///        pre-mapped to the linearly loaded rom window (ROM_LINEAR_DS_ADDRESS)
///        so reads from the first 2 MB never go through the sd cache at all.
extern void* sdc_romBlockToCacheBlock[SDC_ROM_BLOCK_COUNT];

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Loads a rom block into the cache. This method
///        assumes the block is not already in the cache.
/// @param romAddress An address in the rom to load to the cache.
/// @return A pointer to the newly loaded block.
const void* sdc_loadRomBlockDirect(u32 romAddress);

/// @brief Permanently loads the rom block that contains the given romAddress
///        into the sd cache for the purpose of applying patches.
/// @param romAddress An address in the block to load permanently into the cache.
/// @return A pointer to romAddress in the cache block.
void* sdc_loadRomBlockForPatching(u32 romAddress);

/// @brief Writes a patch to the given GBA rom address and remembers it so it is
///        permanently preserved. The patch is reapplied to the containing rom
///        block every time that block is loaded into the sd cache, which makes
///        it survive eviction from the paged high-rom cache. For addresses in
///        the linearly mapped window the copy in main memory is also updated so
///        the executing code sees the patch.
/// @param gbaAddress The GBA address to patch. Must be aligned for `size`.
/// @param value The value to write.
/// @param size The number of bytes to write (1, 2 or 4).
void sdc_patchRom(u32 gbaAddress, u32 value, u32 size);

/// @brief Searches the whole rom (the linearly mapped window and the paged high
///        rom) for a 16 byte signature. This lets patchers find functions that
///        live above the first 2 MB instead of assuming they are linear.
/// @param signature A 16 byte signature.
/// @return The GBA address of the first match, or 0 when not found.
u32 sdc_searchRomSignature(const u32* signature);

/// @brief Drops the rom block mapping of a range of sd cache blocks. This is
///        required after a scratch writer (for example the 64 KB save tag
///        scanner, which uses sdc_cache directly) has overwritten cache blocks
///        without going through the mapping, otherwise the mapping would keep
///        serving the corrupted bytes to the guest.
/// @param firstBlock The first cache block to invalidate.
/// @param blockCount The number of cache blocks to invalidate.
void sdc_invalidateCacheBlockRange(u32 firstBlock, u32 blockCount);

static inline const void* sdc_getRomBlock(u32 romAddress)
{
    u32 romBlock = ((romAddress << 7) >> 7) >> SDC_BLOCK_SHIFT;
    void* data = sdc_romBlockToCacheBlock[romBlock];
    if (data)
        return data;
// #ifdef GBAR3_HICODE_CACHE_MAPPING
//     hic_unmapRomBlock();
// #endif
//     ic_invalidateAll();
    return sdc_loadRomBlockDirect(romAddress);
}

static inline const void* sdc_getRomBlockWithoutIrqYielding(u32 romAddress)
{
    u32 romBlock = ((romAddress << 7) >> 7) >> SDC_BLOCK_SHIFT;
    const void* data = sdc_romBlockToCacheBlock[romBlock];
    if (__builtin_expect(!data, false))
    {
        bool irqYieldingState = vm_disableIrqYielding();
        data = sdc_loadRomBlockDirect(romAddress);
        vm_restoreIrqYielding(irqYieldingState);
    }

    return data;
}

// Protection against a nested IRQ-driven rom load replacing a block that an
// outer transfer is still reading is handled inside SdCache itself. In addition
// to sdc_loadRomBlockDirect() pinning its own in-flight block during nested IRQ
// dispatch, callers that hold a block pointer across an IRQ (the DMA fast path)
// pin the active rom block explicitly.

/// @brief Bounded LIFO pin stack of rom blocks whose cache buffer must not be
///        replaced while an enclosing operation is still reading from it. This
///        is what lets dma_immTransferRomSrc keep copying out of a 4 KB block
///        across a nested IRQ (HBlank/VBlank/Sound DMA) without the block being
///        evicted underneath it. The depth is bounded so the number of pinned
///        blocks can never approach SDC_MIN_EVICTABLE_BLOCKS, and every pin is
///        paired with an unpin that restores the parent's depth.
#define SDC_MAX_ACTIVE_ROM_LOADS    8
extern u32 sdc_activeRomLoadCount;
extern u16 sdc_activeRomLoads[SDC_MAX_ACTIVE_ROM_LOADS];

/// @brief Pins a rom block against cache replacement.
/// @param romBlock The rom block index to pin.
/// @return The previous stack depth, to be passed to sdc_unpinRomBlock().
static inline u32 sdc_pinRomBlock(u32 romBlock)
{
    u32 depth = sdc_activeRomLoadCount;
    if (depth < SDC_MAX_ACTIVE_ROM_LOADS)
    {
        sdc_activeRomLoads[depth] = (u16)romBlock;
        sdc_activeRomLoadCount = depth + 1;
    }
    return depth;
}

/// @brief Unpins a rom block, restoring the depth returned by sdc_pinRomBlock().
static inline void sdc_unpinRomBlock(u32 depth)
{
    if (depth < SDC_MAX_ACTIVE_ROM_LOADS)
        sdc_activeRomLoadCount = depth;
}

/// @brief Initializes the sd cache.
void sdc_init(void);

#ifdef __cplusplus
}
#endif
