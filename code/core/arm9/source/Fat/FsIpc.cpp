#include "common.h"
#include <libtwl/ipc/ipcFifoSystem.h>
#include <libtwl/ipc/ipcFifo.h>
#include <libtwl/ipc/ipcSync.h>
#include <string.h>
#include "IpcChannels.h"
#include "FsIpcCommand.h"
#include "cp15.h"
#include "Cpsr.h"
#include "VirtualMachine/VMNestedIrq.h"
#include "FsIpc.h"

[[gnu::section(".ewram.bss")]]
alignas(32) static fs_ipc_cmd_t sIpcCommand;
[[gnu::section(".ewram.bss")]]
alignas(32) static u8 sTempBuffer[512];

static FsWaitToken* volatile sCurrentWaitToken = nullptr;

static u32 readCommandResult()
{
    // The ARM7 wrote the result after the ARM9 cached the command struct.
    dc_invalidateRange(&sIpcCommand, sizeof(sIpcCommand));
    return sIpcCommand.result;
}

extern "C" u32 fs_getLastResult(void)
{
    return readCommandResult();
}

static bool isArm7FsOperationComplete()
{
    u32 ipcSync = REG_IPCSYNC;
    return ((ipcSync >> IPCSYNC_LOCAL_DATA_SHIFT) & 0xF) == (ipcSync & IPCSYNC_REMOTE_DATA_MASK);
}

extern "C"
[[gnu::noinline]]
u32 fs_waitForCompletion(FsWaitToken* waitToken, bool keepIrqsDisabled)
{
    u32 irqs;
#ifdef GBAR3_IRQ_YIELDING
    if (gIrqYieldingEnabled)
    {
        irqs = arm_disableIrqs();
        vm_disableIrqYielding();
        while (true)
        {
            if (waitToken && waitToken->transactionComplete)
            {
                break;
            }
            FsWaitToken* currentWaitToken = sCurrentWaitToken;
            if (!currentWaitToken)
            {
                break;
            }
            else if (isArm7FsOperationComplete())
            {
                currentWaitToken->transactionComplete = true;
                sCurrentWaitToken = nullptr;
                break;
            }
            if (!(irqs & 0x80) && !vm_yieldGbaIrqs())
            {
                arm_restoreIrqs(irqs);
                irqs = arm_disableIrqs();
            }
        }
        vm_restoreIrqYielding(true);
    }
    else
#endif
    {
        while (true)
        {
            irqs = arm_disableIrqs();
            if (waitToken && waitToken->transactionComplete)
            {
                break;
            }
            FsWaitToken* currentWaitToken = sCurrentWaitToken;
            if (!currentWaitToken)
            {
                break;
            }
            else if (isArm7FsOperationComplete())
            {
                currentWaitToken->transactionComplete = true;
                sCurrentWaitToken = nullptr;
                break;
            }
            arm_restoreIrqs(irqs);
        }
    }

    if (!keepIrqsDisabled)
    {
        arm_restoreIrqs(irqs);
    }

    return irqs;
}

extern "C" u32 fs_waitForCompletionOfCurrentTransaction(bool keepIrqsDisabled)
{
    return fs_waitForCompletion(nullptr, keepIrqsDisabled);
}

static void sendIpcCommand()
{
    dc_flushRange(&sIpcCommand, sizeof(sIpcCommand));
    ipc_setArm9SyncBits(ipc_getArm7SyncBits() + 1);
    ipc_sendWordDirect(((((u32)&sIpcCommand) >> 2) << IPC_FIFO_MSG_CHANNEL_BITS) | IPC_CHANNEL_FS);
}

static void executeIpcCommandAsync(u32 cmd, void* buffer, u32 sector, u32 count, FsWaitToken* waitToken)
{
    waitToken->transactionComplete = false;
    u32 irqs = fs_waitForCompletion(nullptr, true);
    {
        sIpcCommand.cmd = cmd;
        sIpcCommand.buffer = buffer;
        sIpcCommand.sector = sector;
        sIpcCommand.count = count;
        sIpcCommand.result = 0;
        sendIpcCommand();
        sCurrentWaitToken = waitToken;
    }
    arm_restoreIrqs(irqs);
}

extern "C" void fs_readCacheAlignedSectorsAsync(FsDevice device, void* buffer, u32 sector, u32 count, FsWaitToken* waitToken)
{
    dc_invalidateRange(buffer, 512 * count);
    executeIpcCommandAsync(
        device == FS_DEVICE_DLDI ? FS_IPC_CMD_DLDI_READ_SECTORS : FS_IPC_CMD_DSI_SD_READ_SECTORS,
        buffer, sector, count, waitToken);
}

static bool readSectorsNotCacheAligned(FsDevice device, void* buffer, u32 sector, u32 count)
{
    u32 cmd = device == FS_DEVICE_DLDI ? FS_IPC_CMD_DLDI_READ_SECTORS : FS_IPC_CMD_DSI_SD_READ_SECTORS;
    bool ok = true;
    for (u32 i = 0; i < count; i++)
    {
        dc_invalidateRange(sTempBuffer, 512);
        FsWaitToken waitToken;
        executeIpcCommandAsync(cmd, sTempBuffer, sector + i, 1, &waitToken);
        fs_waitForCompletion(&waitToken, false);
        if (readCommandResult() != 0)
        {
            ok = false;
        }
        memcpy((u8*)buffer + 512 * i, sTempBuffer, 512);
    }
    return ok;
}

extern "C" bool fs_readSectors(FsDevice device, void* buffer, u32 sector, u32 count)
{
    if (count == 0)
        return true;

    if (((u32)buffer >> 24) != 2 || ((u32)buffer & 0x1F))
    {
        return readSectorsNotCacheAligned(device, buffer, sector, count);
    }

    FsWaitToken waitToken;
    fs_readCacheAlignedSectorsAsync(device, buffer, sector, count, &waitToken);
    fs_waitForCompletion(&waitToken, false);
    return readCommandResult() == 0;
}

extern "C" void fs_writeCacheAlignedSectorsAsync(FsDevice device, const void* buffer, u32 sector, u32 count, FsWaitToken* waitToken)
{
    dc_flushRange(buffer, 512 * count);
    executeIpcCommandAsync(
        device == FS_DEVICE_DLDI ? FS_IPC_CMD_DLDI_WRITE_SECTORS : FS_IPC_CMD_DSI_SD_WRITE_SECTORS,
        (void*)buffer, sector, count, waitToken);
}

static bool writeSectorsNotCacheAligned(FsDevice device, const void* buffer, u32 sector, u32 count)
{
    u32 cmd = device == FS_DEVICE_DLDI ? FS_IPC_CMD_DLDI_WRITE_SECTORS : FS_IPC_CMD_DSI_SD_WRITE_SECTORS;
    bool ok = true;
    for (u32 i = 0; i < count; i++)
    {
        memcpy(sTempBuffer, (const u8*)buffer + 512 * i, 512);
        dc_flushRange(sTempBuffer, 512);
        FsWaitToken waitToken;
        executeIpcCommandAsync(cmd, sTempBuffer, sector + i, 1, &waitToken);
        fs_waitForCompletion(&waitToken, false);
        if (readCommandResult() != 0)
        {
            ok = false;
        }
    }
    return ok;
}

extern "C" bool fs_writeSectors(FsDevice device, const void* buffer, u32 sector, u32 count)
{
    if (count == 0)
        return true;
    if (((u32)buffer >> 24) != 2 || ((u32)buffer & 0x1F))
    {
        return writeSectorsNotCacheAligned(device, buffer, sector, count);
    }

    FsWaitToken waitToken;
    fs_writeCacheAlignedSectorsAsync(device, buffer, sector, count, &waitToken);
    fs_waitForCompletion(&waitToken, false);
    return readCommandResult() == 0;
}
