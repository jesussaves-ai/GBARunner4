#include "common.h"
#include <string.h>
#include <libtwl/ipc/ipcFifoSystem.h>
#include <libtwl/ipc/ipcFifo.h>
#include <libtwl/ipc/ipcSync.h>
#include "Emulator/IoRegisters.h"
#include "IpcChannels.h"
#include "GbaIoRegOffsets.h"
#include "GbaSoundDefinitions.h"
#include "GbaSoundIpcCommand.h"
#include "GbaSound.h"

[[gnu::section(".ewram.bss"), gnu::aligned(32)]]
gbas_shared_t gGbaSoundShared;

static const u8 sRegisterMasks[] =
{
    (u8)~0x80, (u8)~0xFF, (u8)~0x3F, (u8)~0x00, (u8)~0xFF, (u8)~0xBF, (u8)~0xFF, (u8)~0xFF,
    (u8)~0x3F, (u8)~0x00, (u8)~0xFF, (u8)~0xFF, (u8)~0xFF, (u8)~0xBF, (u8)~0xFF, (u8)~0xFF,
    (u8)~0x1F, (u8)~0xFF, (u8)~0xFF, (u8)~0x1F, (u8)~0xFF, (u8)~0xBF, (u8)~0xFF, (u8)~0xFF,
    (u8)~0xFF, (u8)~0x00, (u8)~0xFF, (u8)~0xFF, (u8)~0x00, (u8)~0xBF, (u8)~0xFF, (u8)~0xFF,
    (u8)~0x88, (u8)~0x00, (u8)~0x00, (u8)~0x88, (u8)~0x7F, (u8)~0xFF, (u8)~0xFF, (u8)~0xFF,
    (u8)~0x00, (u8)~0x00, (u8)~0xFF, (u8)~0xFF, (u8)~0xFF, (u8)~0xFF, (u8)~0xFF, (u8)~0xFF,
    (u8)~0x00, (u8)~0x00, (u8)~0x00, (u8)~0x00, (u8)~0x00, (u8)~0x00, (u8)~0x00, (u8)~0x00,
    (u8)~0x00, (u8)~0x00, (u8)~0x00, (u8)~0x00, (u8)~0x00, (u8)~0x00, (u8)~0x00, (u8)~0x00
};

typedef u32 __attribute__((may_alias)) u32Alias;

static void resetFifo(gbas_direct_channel_t* channel)
{
    // readOffset and writeOffset are adjacent, so clear both with one store.
    *(volatile u32Alias*)&channel->readOffset = 0;
    // Tell the ARM7 to drop the tail of the previous stream that is still cached
    // in its playback word, so restarting direct sound does not click.
    channel->fifoReset = true;
}

static void writeFifo32(gbas_direct_channel_t* channel, u32 val, u32 mask)
{
    u32 writeOffset = channel->writeOffset;
    u32 old = channel->fifo[writeOffset];
    old &= ~mask;
    old |= val & mask;
    channel->fifo[writeOffset] = old;
    channel->writeOffset = (writeOffset + 1) & GBAS_DIRECT_FIFO_MASK;
}

// Registers in 0x80..0x8F that are written even while the master enable is off.
static const u16 sMasterEnableBypassMask = 0x0F1C; // 0x82, 0x83, 0x84, 0x88..0x8B

static __attribute__((noinline,optimize("Os"))) void writeGbAudioReg(u8 reg, u8 val)
{
    if (reg == (GBA_REG_OFFS_SOUNDCNT_H + 1))
    {
        emu_ioRegisters[reg] = val & 0x77;

        if (val & (GBA_SOUNDCNT_H_DIRECT_A_RESET >> 8))
            resetFifo(&gGbaSoundShared.directChannels[0]);

        if (val & (GBA_SOUNDCNT_H_DIRECT_B_RESET >> 8))
            resetFifo(&gGbaSoundShared.directChannels[1]);
        return;
    }

    if (!gGbaSoundShared.masterEnable && reg < 0x90 &&
        (reg < 0x80 || !(sMasterEnableBypassMask & (1u << (reg & 0x0F)))))
        return;

    if (reg == GBA_REG_OFFS_SOUNDCNT_X)
    {
        u32 masterEnable = val & GBA_SOUNDCNT_X_MASTER_ENABLE;
        emu_ioRegisters[reg] = masterEnable;
        gGbaSoundShared.masterEnable = masterEnable != 0;
    }
    else
    {
        emu_ioRegisters[reg] = val & sRegisterMasks[reg - GBA_REG_OFFS_SOUND1CNT_L];
    }
}

void gbas_init(void)
{
    memset(&gGbaSoundShared, 0, sizeof(gGbaSoundShared));
    resetFifo(&gGbaSoundShared.directChannels[0]);
    resetFifo(&gGbaSoundShared.directChannels[1]);
    ipc_sendWordDirect(
        ((((u32)&gGbaSoundShared) >> 5) << (IPC_FIFO_MSG_CHANNEL_BITS + 3)) |
        (GBA_SOUND_IPC_CMD_SETUP << IPC_FIFO_MSG_CHANNEL_BITS) |
        IPC_CHANNEL_GBA_SOUND);
    while (ipc_isRecvFifoEmpty());
    ipc_recvWordDirect();
    ipc_enableArm7Irq();
}

__attribute__((optimize("Os"))) void gbas_writeSoundRegister(u32 address, u32 value, u32 length)
{
    address -= 0x04000000;
    if (address >= GBA_REG_OFFS_FIFO_A && address <= (GBA_REG_OFFS_FIFO_B + 3))
    {
        gbas_direct_channel_t* channel;
        if (address & 4)
        {
            channel = &gGbaSoundShared.directChannels[1];
        }
        else
        {
            channel = &gGbaSoundShared.directChannels[0];
        }
        // Note that 32-bit is handled separately (the normal way these registers should be used)
        u32 mask = length == 1 ? 0xFF : 0xFFFF;
        mask <<= (address & 3) * 8;
        value <<= (address & 3) * 8;
        writeFifo32(channel, value, mask);
    }
    else if (address >= GBA_REG_OFFS_SOUND1CNT_L && address < GBA_REG_OFFS_DMA0SAD)
    {
        // send to the arm 7
        while (ipc_isSendFifoFull());
        if (length == 1)
        {
            ipc_sendWordDirect(
                ((value & 0xFF) << (IPC_FIFO_MSG_CHANNEL_BITS + 3 + 8)) |
                ((address & 0xFF) << (IPC_FIFO_MSG_CHANNEL_BITS + 3)) |
                (GBA_SOUND_IPC_CMD_GB_REG_WRITE_8 << IPC_FIFO_MSG_CHANNEL_BITS) |
                IPC_CHANNEL_GBA_SOUND);
        }
        else if (length == 2)
        {
            ipc_sendWordDirect(
                ((value & 0xFFFF) << (IPC_FIFO_MSG_CHANNEL_BITS + 3 + 8)) |
                ((address & 0xFF) << (IPC_FIFO_MSG_CHANNEL_BITS + 3)) |
                (GBA_SOUND_IPC_CMD_GB_REG_WRITE_16 << IPC_FIFO_MSG_CHANNEL_BITS) |
                IPC_CHANNEL_GBA_SOUND);
        }
        else
        {
            ipc_sendWordDirect(
                ((value & 0xFFFF) << (IPC_FIFO_MSG_CHANNEL_BITS + 3 + 8)) |
                ((address & 0xFF) << (IPC_FIFO_MSG_CHANNEL_BITS + 3)) |
                (GBA_SOUND_IPC_CMD_GB_REG_WRITE_16 << IPC_FIFO_MSG_CHANNEL_BITS) |
                IPC_CHANNEL_GBA_SOUND);
            while (ipc_isSendFifoFull());
            ipc_sendWordDirect(
                ((value >> 16) << (IPC_FIFO_MSG_CHANNEL_BITS + 3 + 8)) |
                (((address + 2) & 0xFF) << (IPC_FIFO_MSG_CHANNEL_BITS + 3)) |
                (GBA_SOUND_IPC_CMD_GB_REG_WRITE_16 << IPC_FIFO_MSG_CHANNEL_BITS) |
                IPC_CHANNEL_GBA_SOUND);
        }

        for (u32 i = 0; i < length; ++i)
        {
            writeGbAudioReg((u8)address, (u8)value);
            ++address;
            value >>= 8;
        }
    }
}
