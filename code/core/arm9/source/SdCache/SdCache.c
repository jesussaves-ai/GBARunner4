#include "common.h"
#include <string.h>
#include "Fat/ff.h"
#include "Fat/diskio.h"
#include "Fat/FsIpc.h"
#include "VirtualMachine/VMNestedIrq.h"
#include "cp15.h"
#include "Cpsr.h"
#include "MemFastSearch.h"
#include "MemoryEmulator/RomDefs.h"
#include "SdCache.h"

typedef struct
{
    vu16 cacheBlock;
    vu32 romBlock;
} SdcFetch;

static SdcFetch sCurrentFetch;

[[gnu::section(".vramhi.bss")]]
void* sdc_romBlockToCacheBlock[SDC_ROM_BLOCK_COUNT];

/// @brief Random generator state for random cache replacement.
static u32 sRandomState;

/// @brief Maps sd cache blocks to rom blocks. Placed in .ewram.bss because the
///        128 KB vrama runtime region that holds the default .bss is very tight.
[[gnu::section(".ewram.bss")]]
static u16 sCacheBlockToRomBlock[SDC_BLOCK_COUNT];

/// @brief The number of usable blocks in the cache. This can be less than the
///        total number of cache blocks when some blocks are permanently loaded.
static u32 sBlockCount;

/// @brief The minimum number of replaceable cache blocks that must remain
///        available. Blocks at and above this index are permanently reserved
///        for patched rom functions. Keeping a floor prevents multi-function
///        high-rom patching from underflowing sBlockCount or starving the
///        replaceable part of the cache.
#define SDC_MIN_EVICTABLE_BLOCKS    16

static u32 sTabuLevel;
static u32 sTabuBlocks[2];

/// @brief Rom blocks whose cache buffer is currently in flight (or has just
///        been loaded) and must therefore not be picked as a replacement victim
///        while an enclosing rom load still reads it. A small stack is used so
///        that nested HBlank/VBlank/Sound DMA rom loads can neither evict an
///        outer transfer's active block nor clobber the outer protection on
///        exit. The bounded depth also bounds the number of simultaneously
///        forbidden blocks, so a huge transfer can never forbid more blocks than
///        the evictable pool (which is always >= SDC_MIN_EVICTABLE_BLOCKS).
#define SDC_MAX_ACTIVE_ROM_LOADS    4
static u32 sActiveRomLoadCount;
static u16 sActiveRomLoads[SDC_MAX_ACTIVE_ROM_LOADS];

[[gnu::section(".ewram.bss")]]
static DWORD sClusterTable[512];

/// @brief Maximum number of rom patches that are remembered and reapplied to a
///        rom block every time it is loaded into the sd cache. The list lives in
///        .ewram.bss so it costs none of the scarce ITCM or vrama space. The
///        apply loop only walks the recorded entries, so a generous capacity is
///        free at runtime.
#define SDC_ROM_PATCH_LIST_SIZE     256

/// @brief A patch that must be applied to a rom block whenever the block is
///        (re)loaded into the cache. `romOffset` is the rom offset (the gba
///        address with the region/mirror bits removed).
typedef struct
{
    u32 romOffset;
    u32 value;
    u32 size;
} SdcRomPatchEntry;

[[gnu::section(".ewram.bss")]]
static SdcRomPatchEntry sRomPatchEntries[SDC_ROM_PATCH_LIST_SIZE];

/// @brief Number of recorded rom patches. Kept in .ewram.bss together with the
///        list because vrama (the 128 KB runtime region) is very tight.
[[gnu::section(".ewram.bss")]]
static u32 sRomPatchCount;

// temporarily
extern FIL gFile;

static void writePatchValue(void* dst, u32 value, u32 size)
{
    if (size == 4)
    {
        *(u32*)dst = value;
    }
    else if (size == 2)
    {
        *(u16*)dst = (u16)value;
    }
    else
    {
        *(u8*)dst = (u8)value;
    }
}

/// @brief Reapplies every remembered rom patch that falls inside the given rom
///        block. Called right after a block has been filled with rom data, so
///        patches survive eviction from the (paged) rom cache.
static void applyRomPatches(u8* block, u32 romBlock)
{
    u32 blockStart = romBlock * SDC_BLOCK_SIZE;
    u32 blockEnd = blockStart + SDC_BLOCK_SIZE;
    for (u32 i = 0; i < sRomPatchCount; i++)
    {
        u32 offset = sRomPatchEntries[i].romOffset;
        if (offset >= blockStart && offset < blockEnd)
        {
            writePatchValue(&block[offset - blockStart], sRomPatchEntries[i].value,
                sRomPatchEntries[i].size);
        }
    }
}

void sdc_patchRom(u32 gbaAddress, u32 value, u32 size)
{
    // A 2-byte Thumb SWI patch may be written at an odd-aligned address; the
    // caller is responsible for aligning. Remember the patch so that it is
    // reapplied whenever the containing block is reloaded after an eviction.
    u32 romOffset = (gbaAddress << 7) >> 7;
    if (sRomPatchCount < SDC_ROM_PATCH_LIST_SIZE)
    {
        sRomPatchEntries[sRomPatchCount].romOffset = romOffset;
        sRomPatchEntries[sRomPatchCount].value = value;
        sRomPatchEntries[sRomPatchCount].size = size;
        sRomPatchCount++;
    }

    // Apply to the sd cache block that serves both data reads and paged
    // high-rom execution, so `hic_mapRomBlock` always sees the patched code.
    writePatchValue(sdc_loadRomBlockForPatching(gbaAddress), value, size);

    // For the linearly mapped window the GBA executes the copy in main memory,
    // so keep that one in sync as well.
    if (gbaAddress >= ROM_LINEAR_GBA_ADDRESS && gbaAddress < ROM_LINEAR_END_GBA_ADDRESS)
    {
        writePatchValue(
            (u8*)(gbaAddress - ROM_LINEAR_GBA_ADDRESS + ROM_LINEAR_DS_ADDRESS), value, size);
    }
}

u32 sdc_searchRomSignature(const u32* signature)
{
    u32 romSize = f_size(&gFile);
    u32 linearSize = romSize < ROM_LINEAR_SIZE ? romSize : ROM_LINEAR_SIZE;
    const u32* function = mem_fastSearch16((const u32*)ROM_LINEAR_DS_ADDRESS, linearSize, signature);
    if (function)
    {
        return (u32)function - ROM_LINEAR_DS_ADDRESS + ROM_LINEAR_GBA_ADDRESS;
    }

    // A 16 byte signature that starts in the last 12 bytes of a 4 KB rom block
    // straddles into the next block. Each block is searched on its own, so such a
    // signature would be missed; carry the previous block's tail to also test the
    // three boundary positions.
    u32 prevTail[4];
    u32 prevTailStart = 0;
    bool hasPrev = false;
    for (u32 i = ROM_LINEAR_END_GBA_ADDRESS; i < ROM_LINEAR_GBA_ADDRESS + romSize; i += SDC_BLOCK_SIZE)
    {
        const u32* block = (const u32*)sdc_getRomBlock(i);
        const u32* match = mem_fastSearch16(block, SDC_BLOCK_SIZE, signature);
        if (match)
        {
            return i + (u32)match - (u32)block;
        }

        if (hasPrev)
        {
            for (u32 j = 1; j <= 3; ++j)
            {
                const u32 copyFromPrev = 16 - j * 4;
                u32 combined[4];
                memcpy(combined, (const u8*)prevTail + j * 4, copyFromPrev);
                memcpy((u8*)combined + copyFromPrev, block, j * 4);
                if (combined[0] == signature[0] && combined[1] == signature[1] &&
                    combined[2] == signature[2] && combined[3] == signature[3])
                {
                    return prevTailStart + j * 4;
                }
            }
        }

        memcpy(prevTail, (const u8*)block + (SDC_BLOCK_SIZE - 16), 16);
        prevTailStart = i + SDC_BLOCK_SIZE - 16;
        hasPrev = true;
    }

    return 0;
}

void sdc_invalidateCacheBlockRange(u32 firstBlock, u32 blockCount)
{
    u32 end = firstBlock + blockCount;
    if (end > SDC_BLOCK_COUNT)
    {
        end = SDC_BLOCK_COUNT;
    }
    for (u32 i = firstBlock; i < end; i++)
    {
        u32 romBlock = sCacheBlockToRomBlock[i];
        if (romBlock != SDC_ROM_BLOCK_INVALID)
        {
            sdc_romBlockToCacheBlock[romBlock] = NULL;
            sCacheBlockToRomBlock[i] = SDC_ROM_BLOCK_INVALID;
        }
    }
    dc_drainWriteBuffer();
}

/// @brief Returns a cache block to replace.
/// @return The index of the cache block to replace.
static u32 getBlockToReplace(void)
{
    sRandomState = sRandomState * 1566083941u + 2531011u;
    u32 maxPlusOne = sBlockCount;
    if (sTabuBlocks[0] != SDC_BLOCK_INVALID)
    {
        maxPlusOne--;
    }
    if (sTabuLevel > 0 && sTabuBlocks[1] != SDC_BLOCK_INVALID)
    {
        maxPlusOne--;
    }
    u32 block = ((sRandomState >> 16) * maxPlusOne) >> 16;
    if (block == sTabuBlocks[0])
    {
        block = maxPlusOne;
    }
    else if (sTabuLevel > 0 && block == sTabuBlocks[1])
    {
        block = maxPlusOne + 1;
    }

    return block;
}

static bool isCurrentlyFetching(void)
{
    return sCurrentFetch.cacheBlock != SDC_BLOCK_INVALID;
}

__attribute__((noinline)) static void finishFetch()
{
    sCacheBlockToRomBlock[sCurrentFetch.cacheBlock] = sCurrentFetch.romBlock;
    sdc_romBlockToCacheBlock[sCurrentFetch.romBlock] = &sdc_cache[sCurrentFetch.cacheBlock][0];
    applyRomPatches(&sdc_cache[sCurrentFetch.cacheBlock][0], sCurrentFetch.romBlock);
    sCurrentFetch.romBlock = SDC_ROM_BLOCK_INVALID;
    sCurrentFetch.cacheBlock = SDC_BLOCK_INVALID;
    dc_drainWriteBuffer();
}

__attribute__((noinline)) static u32 getSdSectorOfRomBlock(u32 romBlock)
{
    u32 romOffset = romBlock * SDC_BLOCK_SIZE;
    if (romOffset >= f_size(&gFile))
    {
        return 0;
    }

    FATFS* fs = gFile.obj.fs;
    u32* tbl = gFile.cltbl + 1;
    u32 csect = (UINT)(romOffset / 512 & (fs->csize - 1));
    u32 cshift = __builtin_ctz(fs->csize) + 9;
    u32 cl = (DWORD)(romOffset >> cshift);
    while (true)
    {
        u32 ncl = *tbl++;
        if (cl < ncl)
        {
            break;
        }
        cl -= ncl;
        tbl++;
    }
    u32 cluster = cl + *tbl - 2;
    u32 sector = fs->database + fs->csize * cluster;
    sector += csect;
    return sector;
}

__attribute__((noinline)) static void fillOutOfBoundsCacheBlock(u32 romBlock, u32 cacheBlock)
{
    u32 romSize = f_size(&gFile);
    u32 powerOf2RomSize = romSize < 0x100000 ? 0x100000 : (1 << (32 - __builtin_clz(romSize - 1)));
    if (romBlock * SDC_BLOCK_SIZE < powerOf2RomSize)
    {
        memset(&sdc_cache[cacheBlock][0], 0xFF, SDC_BLOCK_SIZE);
    }
    else
    {
        for (u32 i = 0; i < SDC_BLOCK_SIZE; i += 4)
        {
            u32 address = romBlock * SDC_BLOCK_SIZE + i;
            u32 oobValue = (address >> 1) & 0xFFFF;
            *(u32 *)&sdc_cache[cacheBlock][i] = oobValue | ((oobValue + 1) << 16);
        }
    }

    sCacheBlockToRomBlock[cacheBlock] = romBlock;
    sdc_romBlockToCacheBlock[romBlock] = &sdc_cache[cacheBlock][0];
    applyRomPatches(&sdc_cache[cacheBlock][0], romBlock);
    dc_drainWriteBuffer();
}

/// @brief Loads a rom block to the given buffer.
/// @param romBlock Rom block index to load.
/// @param dst The destination buffer.
static void* loadRomBlock(u32 romBlock, u32 cacheBlock)
{
    u32 sector = getSdSectorOfRomBlock(romBlock);

    u32 irqs = fs_waitForCompletionOfCurrentTransaction(true);
    if (isCurrentlyFetching())
    {
        finishFetch();
    }

    void* currentCacheBlock = sdc_romBlockToCacheBlock[romBlock];
    if (currentCacheBlock)
    {
        arm_restoreIrqs(irqs);
        return currentCacheBlock;
    }

    if (cacheBlock == SDC_BLOCK_INVALID)
    {
        cacheBlock = getBlockToReplace();
        // Do not replace a block that an enclosing (possibly nested) rom load is
        // still reading from. The active-load set has at most
        // SDC_MAX_ACTIVE_ROM_LOADS entries, so this can never forbid so many
        // blocks that getBlockToReplace() cannot find a victim.
        while (true)
        {
            u32 oldRomBlock = sCacheBlockToRomBlock[cacheBlock];
            if (oldRomBlock == SDC_ROM_BLOCK_INVALID)
            {
                break;
            }
            u32 activeRomLoads = sActiveRomLoadCount;
            bool isActiveRomLoad = false;
            for (u32 i = 0; i < activeRomLoads; i++)
            {
                if (sActiveRomLoads[i] == oldRomBlock)
                {
                    isActiveRomLoad = true;
                    break;
                }
            }
            if (!isActiveRomLoad)
            {
                break;
            }
            cacheBlock = getBlockToReplace();
        }
    }
    u32 oldRomBlock = sCacheBlockToRomBlock[cacheBlock];
    if (oldRomBlock != SDC_ROM_BLOCK_INVALID)
    {
        sdc_romBlockToCacheBlock[oldRomBlock] = NULL;
        sCacheBlockToRomBlock[cacheBlock] = SDC_ROM_BLOCK_INVALID;
    }

    FsWaitToken waitToken;
    if (sector != 0)
    {
        fs_readCacheAlignedSectorsAsync(
            gFile.obj.fs->pdrv == DEV_FAT ? FS_DEVICE_DLDI : FS_DEVICE_DSI_SD,
            &sdc_cache[cacheBlock][0], sector,
            SDC_BLOCK_SIZE / 512, &waitToken);
        sCurrentFetch.romBlock = romBlock;
        sCurrentFetch.cacheBlock = cacheBlock;
    }

    bool decreaseTabuLevel = false;
    if ((arm_getCpsr() & 0x1F) != 0x12 && sTabuLevel < 2)
    {
        sTabuBlocks[sTabuLevel++] = cacheBlock;
        decreaseTabuLevel = true;
    }

    arm_restoreIrqs(irqs);
    if (sector != 0)
    {
        irqs = fs_waitForCompletion(&waitToken, true);
        if (sCurrentFetch.romBlock == romBlock)
        {
            finishFetch();
        }
        arm_restoreIrqs(irqs);
    }
    else
    {
        fillOutOfBoundsCacheBlock(romBlock, cacheBlock);
    }

    if (decreaseTabuLevel)
    {
        sTabuLevel--;
    }

    return &sdc_cache[cacheBlock][0];
}

extern void logAddress(u32 address);

const void* sdc_loadRomBlockDirect(u32 romAddress)
{
    vm_enableNestedIrqs();
    // logAddress(romAddress);
    u32 romBlock = ((romAddress << 7) >> 7) / SDC_BLOCK_SIZE;

    // Protect the block being loaded (and the cache buffer it is being filled
    // into) from replacement by a nested IRQ driven rom load until we are done
    // with it. The stack depth is restored on exit, so nesting is re-entrant and
    // a nested HBlank/VBlank/Sound DMA restores the enclosing protection.
    u32 activeRomLoads = sActiveRomLoadCount;
    if (activeRomLoads < SDC_MAX_ACTIVE_ROM_LOADS)
    {
        sActiveRomLoads[activeRomLoads] = romBlock;
        sActiveRomLoadCount = activeRomLoads + 1;
    }

    void* cacheBlock = loadRomBlock(romBlock, SDC_BLOCK_INVALID);
    // vm_disableNestedIrqs() can still dispatch a pending irq, so the block must
    // stay protected until after it returns.
    vm_disableNestedIrqs();

    sActiveRomLoadCount = activeRomLoads;
    return cacheBlock;
}

void* sdc_loadRomBlockForPatching(u32 romAddress)
{
    u32 romBlock = ((romAddress << 7) >> 7) / SDC_BLOCK_SIZE;
    void* data = sdc_romBlockToCacheBlock[romBlock];
    // if not loaded at all yet, or not permanent
    if (!data || (u32)data < (u32)&sdc_cache[sBlockCount][0])
    {
        if (data)
        {
            // if already loaded, but not permanent, invalidate block
            sdc_romBlockToCacheBlock[romBlock] = NULL;
            sCacheBlockToRomBlock[((u32)data - (u32)&sdc_cache[0][0]) / SDC_BLOCK_SIZE] = SDC_ROM_BLOCK_INVALID;
        }

        if (sBlockCount > SDC_MIN_EVICTABLE_BLOCKS)
        {
            // Permanently reserve the next free block from the top of the cache.
            data = loadRomBlock(romBlock, --sBlockCount);
        }
        else
        {
            // The permanent patch area is full. Fall back to a normally
            // replaceable block so the cache can neither underflow nor starve;
            // the patch is still applied and survives until the block is reused.
            data = loadRomBlock(romBlock, SDC_BLOCK_INVALID);
        }
    }
    return (void*)((u32)data + (romAddress & SDC_BLOCK_MASK));
}

void sdc_init(void)
{
    sRandomState = 0xA512ED48; // initial random seed
    sBlockCount = SDC_BLOCK_COUNT;
    for (u32 i = 0; i < SDC_ROM_BLOCK_COUNT; i++)
    {
        sdc_romBlockToCacheBlock[i] = NULL;
    }
    for (u32 i = 0; i < SDC_BLOCK_COUNT; i++)
    {
        sCacheBlockToRomBlock[i] = SDC_ROM_BLOCK_INVALID;
    }

    sCurrentFetch.cacheBlock = SDC_BLOCK_INVALID;
    sCurrentFetch.romBlock = SDC_ROM_BLOCK_INVALID;
    sRomPatchCount = 0;
    sActiveRomLoadCount = 0;
    sTabuLevel = 0;
    sTabuBlocks[0] = SDC_BLOCK_INVALID;
    sTabuBlocks[1] = SDC_BLOCK_INVALID;
    gIrqYieldingEnabled = true;

    sClusterTable[0] = sizeof(sClusterTable) / sizeof(DWORD);
    gFile.cltbl = sClusterTable;
    u32 result = f_lseek(&gFile, CREATE_LINKMAP);
    if (result != FR_OK)
    {
        logAddress(0xDEADBEEF);
        logAddress(result);
    }
}
