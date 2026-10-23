#include "common.h"
#include <memory>
#include <cstdio>
#include <nds.h>
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
///        does not run the GBA BIOS, so this is just a placeholder buffer.
u32 gGbaBios[16 * 1024 / 4] alignas(256);

/// @brief Result sink read by the automation harness (via the melonDS ARM9 GDB
///        stub) so the suite can report pass/fail without nocash/semihosting.
///        [0] = magic 'TEST', [1] = failed count, [2] = total count, [3] = 'DONE'.
extern "C" {
volatile u32 gTestResults[4] = { 0, 0, 0, 0 };
}

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

extern "C" void testMain(int argc, char* argv[])
{
    Environment::Initialize();

    initLogger();
    LOG_DEBUG("ARM9 Start\n");
    memu_initializeArmDispatchTable();
    vm_initializeUndefinedArmTable();
    testing::InitGoogleMock(&argc, argv);
    int failed = RUN_ALL_TESTS();

    auto* unit = testing::UnitTest::GetInstance();
    int total = unit->total_test_count();

    // Publish the result for the automation harness (melonDS ARM9 GDB stub).
    gTestResults[0] = 0x54455354; // 'TEST'
    gTestResults[1] = (u32)failed;
    gTestResults[2] = (u32)total;
    gTestResults[3] = 0x444F4E45; // 'DONE'

    // Print the result on the DS screen so the suite can be run on real
    // hardware (for example a DSpico) with no emulator debug output at all.
    consoleDemoInit();
    printf("\n GBARunner4 ARM9 tests\n");
    printf(" --------------------\n");
    printf(" failed: %d / %d\n\n", failed, total);
    for (int ts = 0; ts < unit->total_test_suite_count(); ++ts)
    {
        const testing::TestSuite* suite = unit->GetTestSuite(ts);
        for (int t = 0; t < suite->total_test_count(); ++t)
        {
            const testing::TestInfo* info = suite->GetTestInfo(t);
            if (info->result()->Failed())
                printf(" FAIL %s.%s\n", suite->name(), info->name());
        }
    }
    printf("\n %s\n", failed == 0 ? "*** ALL PASS ***" : "*** FAILURES ***");

    // send semihosting exit command
    u32 agbMem = *(u32*)0x027FFF7C;
    *(vu16*)(agbMem + 0x10002) = 0xFF; // exit command
    *(vu16*)(agbMem + 0x10000) = 0x55;

    while (true);
}
