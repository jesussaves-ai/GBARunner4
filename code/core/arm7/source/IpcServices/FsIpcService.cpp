#include "common.h"
#include "../mmc/sdmmc.h"
#include <libtwl/ipc/ipcSync.h>
#include <string.h>
#include "dldi.h"
#include "FsIpcService.h"

void FsIpcService::Start()
{
    if (isDSiMode())
    {
        SDMMC_init(SDMMC_DEV_CARD);
    }
    ThreadIpcService::Start();
}

void FsIpcService::HandleMessage(u32 data)
{
    auto cmd = reinterpret_cast<fs_ipc_cmd_t*>(data << 2);
    switch (cmd->cmd)
    {
        case FS_IPC_CMD_DLDI_SETUP:
        {
            SetupDldi(cmd);
            break;
        }
        case FS_IPC_CMD_DLDI_READ_SECTORS:
        {
            DldiReadSectors(cmd);
            break;
        }
        case FS_IPC_CMD_DLDI_WRITE_SECTORS:
        {
            DldiWriteSectors(cmd);
            break;
        }
        case FS_IPC_CMD_DSI_SD_READ_SECTORS:
        {
            DsiSdReadSectors(cmd);
            break;
        }
        case FS_IPC_CMD_DSI_SD_WRITE_SECTORS:
        {
            DsiSdWriteSectors(cmd);
            break;
        }
    }
}

void FsIpcService::SetupDldi(const fs_ipc_cmd_t* cmd) const
{
    memcpy(_dldi_start, cmd->buffer, sizeof(_dldi_start));
    bool result = _DLDI_startup_ptr();
    SendResponseMessage(result);
}

void FsIpcService::DldiReadSectors(fs_ipc_cmd_t* cmd) const
{
    cmd->result = _DLDI_readSectors_ptr(cmd->sector, cmd->count, cmd->buffer) ? 0 : 1;
    ipc_setArm7SyncBits(ipc_getArm9SyncBits());
}

void FsIpcService::DldiWriteSectors(fs_ipc_cmd_t* cmd) const
{
    cmd->result = _DLDI_writeSectors_ptr(cmd->sector, cmd->count, cmd->buffer) ? 0 : 1;
    ipc_setArm7SyncBits(ipc_getArm9SyncBits());
}

void FsIpcService::DsiSdReadSectors(fs_ipc_cmd_t* cmd) const
{
    cmd->result = SDMMC_readSectors(SDMMC_DEV_CARD, cmd->sector, cmd->buffer, cmd->count);
    ipc_setArm7SyncBits(ipc_getArm9SyncBits());
}

void FsIpcService::DsiSdWriteSectors(fs_ipc_cmd_t* cmd) const
{
    cmd->result = SDMMC_writeSectors(SDMMC_DEV_CARD, cmd->sector, cmd->buffer, cmd->count);
    ipc_setArm7SyncBits(ipc_getArm9SyncBits());
}
