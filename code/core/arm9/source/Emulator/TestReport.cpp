#include "TestReport.h"

#ifdef GBAR3_EMBED_ROM

// Must live in DS main RAM so the emulator's system-RAM dump contains it.
// `.ewram.bss` is zeroed by crt0 and is not reachable by guest code.
[[gnu::section(".ewram.bss")]]
volatile TestReport gTestReport;

void test_reportStage(u32 stage)
{
    gTestReport.stage = stage;
    gTestReport.magic = TEST_REPORT_MAGIC;
}

void test_reportResult(u32 result, u32 code)
{
    gTestReport.result = result;
    gTestReport.code = code;
    gTestReport.magic = TEST_REPORT_MAGIC;
}

extern "C" void test_reportFrame(void)
{
    gTestReport.frame++;
}

#endif // GBAR3_EMBED_ROM
