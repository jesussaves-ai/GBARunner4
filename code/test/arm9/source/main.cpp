#include "common.h"
#include <memory>
#include "gtest/gtest.h"
#include "gmock/gmock.h"
#include "core/Environment.h"
#include "logger/PlainLogger.h"
#include "logger/NocashOutputStream.h"
#include "logger/NitroEmulatorOutputStream.h"
#include "logger/NullLogger.h"
#include "MemoryEmulator/Arm/ArmDispatchTable.h"
#include "VirtualMachine/VMUndefinedArmTable.h"

ILogger* gLogger;

/// @brief Referenced by the shared VMDtcm.s BIOS jump vector. The test harness
///        does not run the GBA BIOS, so this is only a placeholder buffer.
u32 gGbaBios[16 * 1024 / 4] alignas(256);

static void initLogger()
{
    std::unique_ptr<IOutputStream> outputStream;
    if (Environment::IsIsNitroEmulator() && Environment::SupportsAgbSemihosting())
    {
        outputStream = std::make_unique<NitroEmulatorOutputStream>();
    }
    else //if (Environment::SupportsNocashPrint())
    { 
        outputStream = std::make_unique<NocashOutputStream>();
    }
    // else
    // {
    //     gLogger = new NullLogger();
    //     return;
    // }
    gLogger = new PlainLogger(LogLevel::All, std::move(outputStream));
}

// ---------------------------------------------------------------------------
// Headless test report.
//
// The DS test ROM normally prints through the no$gba debug ring, which a plain
// libretro frontend cannot see. This struct lives in DS main RAM so tools/lrshot
// can read the result straight out of RETRO_MEMORY_SYSTEM_RAM. Magic and field
// order are a hard ABI with tools/lrshot.c.
// ---------------------------------------------------------------------------
#define TEST_SUMMARY_MAGIC 0x54535452u  // "RTST"

struct TestSummary
{
    volatile u32 magic;
    volatile u32 stage;      // 1 = testMain entered, 2 = RUN_ALL_TESTS finished
    volatile u32 total;
    volatile u32 passed;
    volatile u32 failed;
    volatile u32 rc;
    volatile char firstFailure[64];
};

[[gnu::section(".ewram.bss")]]
static volatile TestSummary sTestSummary;

static void publishSummary(u32 stage, int rc, const char* firstFailure)
{
    sTestSummary.total = testing::UnitTest::GetInstance()->total_test_count();
    sTestSummary.passed = testing::UnitTest::GetInstance()->successful_test_count();
    sTestSummary.failed = testing::UnitTest::GetInstance()->failed_test_count();
    sTestSummary.rc = (u32)rc;
    u32 i = 0;
    if (firstFailure)
        while (firstFailure[i] && i < sizeof(sTestSummary.firstFailure) - 1)
        {
            sTestSummary.firstFailure[i] = firstFailure[i];
            i++;
        }
    sTestSummary.firstFailure[i] = 0;
    sTestSummary.stage = stage;
    sTestSummary.magic = TEST_SUMMARY_MAGIC;
}

extern "C" void testMain(int argc, char* argv[])
{
    publishSummary(1, 0, nullptr);
    Environment::Initialize();

    initLogger();
    LOG_DEBUG("ARM9 Start\n");
    memu_initializeArmDispatchTable();
    vm_initializeUndefinedArmTable();
    testing::InitGoogleMock(&argc, argv);
    int rc = RUN_ALL_TESTS();

    const char* firstFailure = nullptr;
    const testing::UnitTest* unitTest = testing::UnitTest::GetInstance();
    for (int s = 0; s < unitTest->total_test_suite_count() && !firstFailure; s++)
    {
        const testing::TestSuite* suite = unitTest->GetTestSuite(s);
        if (!suite)
            continue;
        for (int i = 0; i < suite->total_test_count(); i++)
        {
            const testing::TestInfo* info = suite->GetTestInfo(i);
            if (info && !info->result()->Passed())
            {
                firstFailure = info->name();
                break;
            }
        }
    }
    publishSummary(2, rc, firstFailure);

    // send semihosting exit command
    u32 agbMem = *(u32*)0x027FFF7C;
    *(vu16*)(agbMem + 0x10002) = 0xFF; // exit command
    *(vu16*)(agbMem + 0x10000) = 0x55;

    while (true);
}
