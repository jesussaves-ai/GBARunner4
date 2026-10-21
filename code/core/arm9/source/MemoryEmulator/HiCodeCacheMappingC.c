#ifdef GBAR3_HICODE_CACHE_MAPPING

#include "common.h"
#include "cp15.h"
#include "Cpsr.h"
#include "SdCache/SdCache.h"
#include "HiCodeCacheMapping.h"

[[gnu::section(".dtcm")]]
struct
{
    u32 lastHicodeBlock;
} gHicodeState;

/// @brief Scratch pattern (HICODE_UNDEFINED_INSTRUCTION) prefetched into the
///        locked instruction cache. It lives in main RAM (.ewram.bss) rather
///        than the scarce 128 KB vrama region. The 2048-byte alignment is
///        required: initializeItcm() derives the cache index from the address
///        of each prefetched line, so the buffer's low 11 bits must match the
///        explicit cache_setIndex() calls.
[[gnu::section(".ewram.bss"), gnu::aligned(2048)]]
u32 gHicodeUndefinedData[2048 / 4];

static inline u32 mpu_getRegion4(void)
{
    u32 config;
    asm volatile("mrc p15, 0, %0, c6, c4, 0\n" : "=r" (config));
    return config;
}

static inline void mpu_setRegion4(u32 config)
{
    asm volatile("mcr p15, 0, %0, c6, c4, 0\n" :: "r"(config));
}

static inline void clearHicodeCacheSegment(void)
{
    for (u32 i = 0; i < 2048; i += 32)
    {
        ic_prefetch((const u8*)gHicodeUndefinedData + i);
    }
}

static inline void mapNewBlock(u32 gbaAddress, const u8* romBlockData)
{
    mpu_setRegion4((gbaAddress & ~0xFFF) | 0x17);

    u32 hicodeBlockAddress = gbaAddress & ~(HICODE_BLOCK_SIZE - 1);
    romBlockData += hicodeBlockAddress & 0xFFF;

    gHicodeState.lastHicodeBlock = hicodeBlockAddress;

    ic_setLockdown(0, true);

    clearHicodeCacheSegment();

    for (u32 i = 0; i < HICODE_BLOCK_SIZE; i += 32)
    {
        ic_prefetch(&romBlockData[i]);
    }

    ic_setLockdown(2, false);

    u32 tag0 = hicodeBlockAddress | (1 << 4);
    u32 tag1 = tag0 ^ 2048;

    for (u32 i = 0; i < 2048; i += 32)
    {
        cache_setIndex(i);
        ic_setTag(tag0);

        cache_setIndex(i | (1 << 30));
        ic_setTag(tag1);
    }
}

static inline void addBlock(u32 gbaAddress, const u8* romBlockData)
{
    u32 hicodeBlockAddress = gbaAddress & ~(HICODE_BLOCK_SIZE - 1);
    romBlockData += hicodeBlockAddress & 0xFFF;

    ic_setLockdown(0, true);

    u32 tag0 = hicodeBlockAddress | (1 << 4);
    for (u32 i = 0; i < HICODE_BLOCK_SIZE; i += 32)
    {
        ic_prefetch(&romBlockData[i]);
        cache_setIndex((u32)&romBlockData[i]);
        ic_setTag(tag0);
    }

    ic_setLockdown(2, false);
}

ITCM_CODE void hic_mapRomBlock(u32 gbaAddress)
{
    gbaAddress &= ~0x06000000;
    const u8* romBlockData = sdc_getRomBlockWithoutIrqYielding(gbaAddress);

    u32 region4Config = mpu_getRegion4();
    if ((region4Config & 1) && (gHicodeState.lastHicodeBlock & ~0x7FF) == (gbaAddress & ~0x7FF))
    {
        // load extra code in the current cache segment 0
        addBlock(gbaAddress, romBlockData);
    }
    else
    {
        // clear cache segment 0 and load new hicode block
        mapNewBlock(gbaAddress, romBlockData);
    }
}

[[gnu::noinline]]
static ITCM_CODE void initializeItcm(void)
{
    ic_setLockdown(0, true);

    for (u32 i = 0; i < 2048; i += 32)
    {
        ic_prefetch((const u8*)gHicodeUndefinedData + i);
        cache_setIndex(i);
        ic_setTag(0);
    }

    ic_setLockdown(1, true);

    for (u32 i = 0; i < 2048; i += 32)
    {
        ic_prefetch((const u8*)gHicodeUndefinedData + i);
        cache_setIndex(i | (1 << 30));
        ic_setTag(0);
    }

    ic_setLockdown(2, false);
}

void hic_initialize(void)
{
    for (u32 i = 0; i < 2048 / 4; i++)
    {
        gHicodeUndefinedData[i] = HICODE_UNDEFINED_INSTRUCTION;
    }

    u32 irqs = arm_disableIrqs();
    {
        initializeItcm();
    }
    arm_restoreIrqs(irqs);
}

#endif