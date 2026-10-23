#include "common.h"
#include <libtwl/spi/spiPmic.h>
#include "SystemIpcService.h"

// Implemented in main.cpp: requests a warm reset (0) or power off (1).
extern "C" void arm7_requestExit(int powerOff);

void SystemIpcService::HandleMessage(u32 data)
{
    switch (data & 0xF)
    {
        case SYSTEM_IPC_CMD_SET_TOP_BACKLIGHT:
        {
            pmic_setTopBacklightEnable((data >> 4) & 1);
            SendResponseMessage(0);
            break;
        }
        case SYSTEM_IPC_CMD_SET_BOTTOM_BACKLIGHT:
        {
            pmic_setBottomBacklightEnable((data >> 4) & 1);
            SendResponseMessage(0);
            break;
        }
        case SYSTEM_IPC_CMD_RESET:
        {
            arm7_requestExit(0);
            SendResponseMessage(0);
            break;
        }
        case SYSTEM_IPC_CMD_POWER_OFF:
        {
            arm7_requestExit(1);
            SendResponseMessage(0);
            break;
        }
    }
}
