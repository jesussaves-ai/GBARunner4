#include "common.h"
#include <libtwl/ipc/ipcFifo.h>
#include <libtwl/ipc/ipcFifoSystem.h>
#include <string.h>
#include "ff.h"
#include "IpcChannels.h"
#include "cp15.h"
#include "FsIpc.h"
#include "diskio.h"

extern "C" DSTATUS disk_status(BYTE pdrv)
{
    return 0;
}

extern "C" DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv == DEV_FAT)
    {
        // already initialized by the bootstrapper
        return 0;
    }
    else if (pdrv == DEV_SD)
    {
        // already initialized on the arm7
        return 0;
    }
    return STA_NOINIT;
}

extern "C" DRESULT disk_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count)
{
    if (pdrv == DEV_FAT)
    {
        return fs_readSectors(FS_DEVICE_DLDI, buff, sector, count) ? RES_OK : RES_ERROR;
    }
    else if (pdrv == DEV_SD)
    {
        return fs_readSectors(FS_DEVICE_DSI_SD, buff, sector, count) ? RES_OK : RES_ERROR;
    }
    return RES_PARERR;
}

#if FF_FS_READONLY == 0

extern "C" DRESULT disk_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count)
{
    if (pdrv == DEV_FAT)
    {
        return fs_writeSectors(FS_DEVICE_DLDI, buff, sector, count) ? RES_OK : RES_ERROR;
    }
    else if (pdrv == DEV_SD)
    {
        return fs_writeSectors(FS_DEVICE_DSI_SD, buff, sector, count) ? RES_OK : RES_ERROR;
    }
    return RES_PARERR;
}

#endif

extern "C" DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    return RES_OK;
}
