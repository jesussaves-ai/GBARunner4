#include "common.h"
#include "MemoryEmulator/RomDefs.h"
#include "MemoryProtectionConfiguration.h"

namespace
{
constexpr u32 MakeRegionBaseAndSize(u32 regionBase, MpuRegionSize regionSize)
{
    return regionBase | (static_cast<u32>(regionSize) << 1) | 1;
}

struct RegionConfiguration
{
    u32 baseAndSize;
    MpuAccessPermission dataAccessPermission;
    MpuAccessPermission instructionAccessPermission;
    bool instructionCache;
    bool dataCache;
    bool bufferable;
};

// One entry per MPU region, indexed by MpuRegion.
constexpr RegionConfiguration s_regionConfigurations[] = {
    // mpu region 0: ITCM, DTCM, uncached main memory, IO, GBA slot
    {MakeRegionBaseAndSize(0x00000000, MPU_REGION_SIZE_4GB),
        MPU_ACCESS_PERMISSION_PRIV_READ_WRITE, MPU_ACCESS_PERMISSION_PRIV_READ_WRITE, false, false, false},

    // mpu region 1: cached main memory
    {MakeRegionBaseAndSize(0x02000000, MPU_REGION_SIZE_4MB),
        MPU_ACCESS_PERMISSION_PRIV_READ_WRITE, MPU_ACCESS_PERMISSION_PRIV_READ_WRITE, true, true, false},

    // mpu region 2: GBA VRAM
    {MakeRegionBaseAndSize(0x06000000, MPU_REGION_SIZE_8MB),
        MPU_ACCESS_PERMISSION_PRIV_READ_WRITE, MPU_ACCESS_PERMISSION_PRIV_READ_WRITE, false, false, true},

    // mpu region 3: linear part of GBA rom
    {MakeRegionBaseAndSize(ROM_LINEAR_DS_ADDRESS, MPU_REGION_SIZE_2MB),
        MPU_ACCESS_PERMISSION_USER_READ_PRIV_WRITE, MPU_ACCESS_PERMISSION_USER_READ_PRIV_WRITE, true, true, false},

    // mpu region 4: OBJ VRAM (DS)
    {MakeRegionBaseAndSize(0x06400000, MPU_REGION_SIZE_32KB),
#ifdef GBAR3_HICODE_CACHE_MAPPING
        MPU_ACCESS_PERMISSION_PRIV_READ_WRITE,
#else
        MPU_ACCESS_PERMISSION_USER_READ_PRIV_WRITE,
#endif
        MPU_ACCESS_PERMISSION_USER_READ_PRIV_WRITE, true, false, true},

    // mpu region 5: LCDC VRAM A and B
    {MakeRegionBaseAndSize(0x06800000, MPU_REGION_SIZE_1MB),
        MPU_ACCESS_PERMISSION_USER_READ_PRIV_WRITE, MPU_ACCESS_PERMISSION_READ_WRITE, true, true, true},

    // mpu region 6: GBA IWRAM
    {MakeRegionBaseAndSize(0x03000000, MPU_REGION_SIZE_16MB),
        MPU_ACCESS_PERMISSION_READ_WRITE, MPU_ACCESS_PERMISSION_READ_WRITE, true, false, false},

    // mpu region 7: GBA EWRAM
    {MakeRegionBaseAndSize(0x02000000, MPU_REGION_SIZE_256KB),
        MPU_ACCESS_PERMISSION_READ_WRITE, MPU_ACCESS_PERMISSION_READ_WRITE, true, false, false},
};

constexpr u32 s_regionCount = sizeof(s_regionConfigurations) / sizeof(s_regionConfigurations[0]);
static_assert(s_regionCount == 8, "All eight ARM9 MPU regions must be configured.");

constexpr u32 MakeRegionBaseAndSizeConfiguration(u32 index)
{
    return s_regionConfigurations[index].baseAndSize;
}

constexpr u32 MakeAccessPermissionMask(MpuAccessPermission RegionConfiguration::* permission)
{
    u32 mask = 0;
    for (u32 i = 0; i < s_regionCount; i++)
        mask |= static_cast<u32>(s_regionConfigurations[i].*permission) << (i * 4);
    return mask;
}

constexpr u32 MakeEnableMask(bool RegionConfiguration::* enable)
{
    u32 mask = 0;
    for (u32 i = 0; i < s_regionCount; i++)
        mask |= static_cast<u32>(s_regionConfigurations[i].*enable) << i;
    return mask;
}

// Runtime table containing only the values written to CP15 c6 (one per region).
constexpr u32 s_regionBaseAndSizes[s_regionCount] = {
    MakeRegionBaseAndSizeConfiguration(0),
    MakeRegionBaseAndSizeConfiguration(1),
    MakeRegionBaseAndSizeConfiguration(2),
    MakeRegionBaseAndSizeConfiguration(3),
    MakeRegionBaseAndSizeConfiguration(4),
    MakeRegionBaseAndSizeConfiguration(5),
    MakeRegionBaseAndSizeConfiguration(6),
    MakeRegionBaseAndSizeConfiguration(7),
};

constexpr u32 s_dataAccessPermissions =
    MakeAccessPermissionMask(&RegionConfiguration::dataAccessPermission);
constexpr u32 s_instructionAccessPermissions =
    MakeAccessPermissionMask(&RegionConfiguration::instructionAccessPermission);
constexpr u32 s_instructionCacheMask = MakeEnableMask(&RegionConfiguration::instructionCache);
constexpr u32 s_dataCacheMask = MakeEnableMask(&RegionConfiguration::dataCache);
constexpr u32 s_bufferableMask = MakeEnableMask(&RegionConfiguration::bufferable);
}

extern "C" void setupMemoryProtection()
{
    // Each of these CP15 registers holds the attributes for all eight regions,
    // so write their final values in one pass instead of read-modify-writing
    // every region separately.
    asm volatile(
        "mcr p15, 0, %0, c2, c0, 0\n\t" // data cacheable
        "mcr p15, 0, %1, c2, c0, 1\n\t" // instruction cacheable
        "mcr p15, 0, %2, c3, c0, 0\n\t" // write bufferable
        "mcr p15, 0, %3, c5, c0, 2\n\t" // data access permission
        "mcr p15, 0, %4, c5, c0, 3\n\t" // instruction access permission
        :
        : "r"(s_dataCacheMask), "r"(s_instructionCacheMask), "r"(s_bufferableMask),
          "r"(s_dataAccessPermissions), "r"(s_instructionAccessPermissions)
        : "memory");

    for (u32 i = 0; i < s_regionCount; i++)
        mpu_setRegionBaseAndSize(static_cast<MpuRegion>(i), s_regionBaseAndSizes[i]);

    // On DSi/TWL hardware the main memory window is 16 MB, so extend the cached
    // main memory region (region 1) beyond the NTR 4 MB limit. This keeps the
    // SdCache DSi pool at 0x02400000-0x02FFFFFF data/instruction cacheable,
    // matching the NTR pool's behaviour. On NTR the 4 MB setting is preserved.
    // The higher-numbered regions (linear ROM, GBA EWRAM/IWRAM) keep priority.
    if (((*(volatile u32*)0x04004000) & 3) == 1)
    {
        mpu_setRegionBaseAndSize(MPU_REGION_1,
            MakeRegionBaseAndSize(0x02000000, MPU_REGION_SIZE_16MB));
    }
}
