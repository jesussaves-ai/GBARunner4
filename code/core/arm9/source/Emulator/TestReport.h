#pragma once

#include "common.h"

// ---------------------------------------------------------------------------
// Headless-test report channel.
//
// The embedded-ROM test build cannot rely on the DS filesystem: DLDI/SD access
// is exactly the part that does not work in a plain libretro session. Instead
// it publishes its progress through a small struct that lives in DS main RAM
// (`.ewram.bss`), which the libretro frontend can read straight out of
// RETRO_MEMORY_SYSTEM_RAM after N frames.
//
// Layout is a hard ABI between the ARM9 core and tools/lrshot.c (and the
// headless test script). Keep the field order and the magic in sync.
// ---------------------------------------------------------------------------

#define TEST_REPORT_MAGIC   0x47524234u  // "4BRG" in little-endian memory

#define TEST_RESULT_RUNNING 0u
#define TEST_RESULT_PASS    1u
#define TEST_RESULT_FAIL    2u

struct TestReport
{
    volatile u32 magic;
    volatile u32 stage;      // boot progress; 100 = VM started
    volatile u32 result;     // TEST_RESULT_*
    volatile u32 code;       // test number / failure code
    volatile u32 frame;      // VBlank count since the VM started
    volatile u32 reserved;
};

#ifdef GBAR3_EMBED_ROM
extern volatile TestReport gTestReport;

/// @brief Records the latest boot stage (visible even if the VM never starts).
void test_reportStage(u32 stage);

/// @brief Publishes the final result of a test ROM.
void test_reportResult(u32 result, u32 code);

/// @brief Advances the VBlank frame counter (called from the VBlank IRQ).
extern "C" void test_reportFrame(void);
#else
inline void test_reportStage(u32) { }
inline void test_reportResult(u32, u32) { }
#endif
