#include "common.h"
#include <cstring>
#include <gtest/gtest.h>
#include "SdCache/SdCacheDefs.h"
#include "JitPatcher/JitCommon.h"
#include "JitPatcher/JitThumb.h"
#include "JitPatcher/JitArm.h"

// Defined by JitStubs.cpp (SdCache.c is not linked into the tests).
extern u8 (*sdc_cache)[SDC_BLOCK_SIZE];

// Defined in JitThumb.c (ITCM) but not declared in the public header.
extern "C" u16* jit_handleThumbBCond(u16* instructionPtr, u32 instruction, bool conditionPass);

namespace
{
/// @brief Scratch "SdCache" the JIT indexes its bit tables through. Code is
///        placed at offset 0 of the first 4 KB block, which is all the JIT
///        needs to resolve a block's processed/aux bits.
constexpr u32 TEST_CACHE_BYTES = 64 * 1024;
alignas(SDC_BLOCK_SIZE) u8 sTestCache[TEST_CACHE_BYTES];

/// @brief The trap the JIT writes for a Thumb `b cond`.
constexpr u16 THUMB_BCOND_TRAP = 0xB800;

int8_t SignExtend8(u16 value) { return (int8_t)(value & 0xFF); }
}

class JitTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        sdc_cache = (u8(*)[SDC_BLOCK_SIZE])sTestCache;
        std::memset(sTestCache, 0, sizeof(sTestCache));
        jit_init();
    }

    void TearDown() override { sdc_cache = nullptr; }

    /// @brief Clears the processed/aux bits of the first block so the next
    ///        iteration re-processes it (a full jit_init is far too slow here).
    void ResetBlockBits()
    {
        std::memset(sTestCache, 0, sizeof(sTestCache));
        jit_getJitBits((void*)sTestCache)[0] = 0;
        jit_getJitAuxBits((void*)sTestCache)[0] = 0;
    }

    u16* ThumbBlock() { return (u16*)sTestCache; }
};

// --- Thumb `b cond` ------------------------------------------------------------------

TEST_F(JitTest, ThumbBCondEncodingPlacesConditionAndOffsetHighBits)
{
    const u16 conds[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 };
    const u16 offsets[] = { 0x00, 0x02, 0x03, 0x7E, 0x80, 0xFC, 0xFF };

    for (u16 cond : conds)
    {
        for (u16 off : offsets)
        {
            ResetBlockBits();
            u16* p = ThumbBlock();
            *p = 0xD000 | (cond << 8) | off;
            jit_processThumbBlock(p);

            u16 expected = (u16)(THUMB_BCOND_TRAP | (cond << 6) | ((off >> 2) & 0x3F));
            EXPECT_EQ(*p, expected) << "cond=" << (int)cond << " off=" << (int)off;
        }
    }
}

TEST_F(JitTest, ThumbBCondAuxStoresLowTwoOffsetBits)
{
    for (u16 off = 0; off < 256; ++off)
    {
        ResetBlockBits();
        u16* p = ThumbBlock();
        *p = 0xD000 | (1 << 8) | off; // cond = ne
        jit_processThumbBlock(p);

        u32 auxBits = ((*jit_getJitAuxBits(p)) >> ((u32)p & 0xF)) & 3;
        EXPECT_EQ(auxBits, (u32)(off & 3)) << "off=" << (int)off;
    }
}

TEST_F(JitTest, ThumbBCondDecodeRecoversOriginalOffset)
{
    // Every 8-bit offset must round-trip through (patch -> trap decode).
    for (u16 off = 0; off < 256; ++off)
    {
        ResetBlockBits();
        u16* p = ThumbBlock();
        *p = 0xD000 | (1 << 8) | off;
        jit_processThumbBlock(p);

        u16* dest = jit_handleThumbBCond(p, *p, /*conditionPass=*/true);
        int expected = (int)(u32)p + 5 + (int)SignExtend8(off) * 2;
        EXPECT_EQ((u32)dest, (u32)expected) << "off=" << (int)off;
    }
}

// --- ARM control flow ----------------------------------------------------------------

TEST_F(JitTest, ArmBranchIsPatchedToTrap)
{
    // B +0: 0xEA000000 -> the JIT rewrites it to the unconditional-branch trap.
    u32* p = (u32*)sTestCache;
    *p = 0xEA000000;
    jit_processArmBlock(p);
    EXPECT_EQ(*p, 0xEC000000u);
}

TEST_F(JitTest, ArmBxIsPatchedToTrap)
{
    // BX r0: 0xE12FFF10.
    u32* p = (u32*)sTestCache;
    *p = 0xE12FFF10;
    jit_processArmBlock(p);
    EXPECT_EQ(*p, 0xE1B00090u);
}
