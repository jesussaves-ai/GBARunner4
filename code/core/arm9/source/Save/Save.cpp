#include "common.h"
#include <libtwl/ipc/ipcFifo.h>
#include <libtwl/ipc/ipcFifoSystem.h>
#include <string.h>
#include "Fat/ff.h"
#include "Core/Environment.h"
#include "MemFastSearch.h"
#include "SaveSwi.h"
#include "SaveTypeInfo.h"
#include "SaveEeprom.h"
#include "SaveFlash.h"
#include "SaveSram.h"
#include "VirtualMachine/VMNestedIrq.h"
#include "MemoryEmulator/RomDefs.h"
#include "SdCache/SdCache.h"
#include "IpcChannels.h"
#include "GbaSaveIpcCommand.h"
#include "Save.h"

#define DEFAULT_SAVE_SIZE   (32 * 1024)

[[gnu::section(".ewram.bss")]]
u8 gSaveData[SAVE_DATA_SIZE] alignas(32);

[[gnu::section(".ewram.bss")]]
FIL gSaveFile alignas(32);

[[gnu::section(".ewram.bss"), gnu::aligned(32)]]
gba_save_shared_t gGbaSaveShared;

static DWORD sClusterTable[64];
static u32 sSkipSaveCheckInstruction;

// temporarily
extern FIL gFile;

void sav_patchRomFunction(u32 gbaAddress, u32 swiNumber)
{
    gbaAddress &= ~1u;
    sdc_patchRom(gbaAddress, SAVE_THUMB_SWI(swiNumber), 2);
}

bool sav_tryPatchFunction(const u32* signature, u32 saveSwiNumber, void* patchFunction)
{
    // Search the whole rom (linear window + paged high rom) so EEPROM, SRAM and
    // flash functions above the 2 MB ROM_LINEAR_SIZE window are patched in place
    // instead of being mistaken for the linearly mapped copy.
    u32 gbaAddress = sdc_searchRomSignature(signature);
    if (!gbaAddress)
    {
        return false;
    }

    sav_swiTable[saveSwiNumber] = patchFunction;
    sav_patchRomFunction(gbaAddress, saveSwiNumber);
    return true;
}

[[gnu::noinline]]
static void loadSaveClusterMap(void)
{
    sClusterTable[0] = sizeof(sClusterTable) / sizeof(DWORD);
    gSaveFile.cltbl = sClusterTable;
    f_lseek(&gSaveFile, CREATE_LINKMAP);
}

[[gnu::noinline, gnu::optimize("Os")]]
static void fillSaveFile(u32 start, u32 end)
{
    const u8 saveFill = SAVE_DATA_FILL;
    f_lseek(&gSaveFile, start);
    for (u32 i = start; i < end; ++i)
    {
        UINT written = 0;
        f_write(&gSaveFile, &saveFill, 1, &written);
    }
    f_sync(&gSaveFile);
}

[[gnu::noinline]]
static void growSaveFile(u32 start, u32 end)
{
    if (f_lseek(&gSaveFile, end) == FR_OK)
    {
        fillSaveFile(start, end);
    }
}

__attribute__((optimize("Os"))) void sav_initializeSave(const SaveTypeInfo* saveTypeInfo, const char* savePath)
{
    u32 saveSize = saveTypeInfo ? saveTypeInfo->size : DEFAULT_SAVE_SIZE;
    bool isNitro = Environment::IsIsNitroEmulator();
    memset(gSaveData, SAVE_DATA_FILL, SAVE_DATA_SIZE);
    if (isNitro)
    {
        memset((void*)ISNITRO_SAVE_BUFFER, SAVE_DATA_FILL, ISNITRO_SAVE_BUFFER_SIZE);
    }
    memset(&gSaveFile, 0, sizeof(gSaveFile));
    // Outside the Nitro emulator, open an existing file or create a new one.
    u32 openFlags = isNitro
        ? (FA_OPEN_EXISTING | FA_READ | FA_WRITE)
        : (FA_OPEN_ALWAYS | FA_READ | FA_WRITE);
    if (f_open(&gSaveFile, savePath, openFlags) == FR_OK)
    {
        // A newly created file has size 0; an already larger file makes the fill range empty.
        u32 initialSize = f_size(&gSaveFile);
        growSaveFile(initialSize, saveSize);
        loadSaveClusterMap();

        if (saveSize <= SAVE_DATA_SIZE)
        {
            f_rewind(&gSaveFile);
            UINT read = 0;
            f_read(&gSaveFile, gSaveData, saveSize, &read);
        }

        if (isNitro)
        {
            f_rewind(&gSaveFile);
            UINT read = 0;
            f_read(&gSaveFile, (void*)ISNITRO_SAVE_BUFFER, saveSize, &read);
        }
    }

    gGbaSaveShared.saveState = GBA_SAVE_STATE_CLEAN;
    sSkipSaveCheckInstruction = emu_vblankIrqSkipSaveCheckInstruction;
    if (!saveTypeInfo || (saveTypeInfo->type & SAVE_TYPE_MASK) == SAVE_TYPE_SRAM)
    {
        gGbaSaveShared.saveData = gSaveData;
        gGbaSaveShared.saveDataSize = saveSize;
    }
    else
    {
        gGbaSaveShared.saveData = nullptr;
        gGbaSaveShared.saveDataSize = 0;
    }

    ipc_sendWordDirect(
        ((((u32)&gGbaSaveShared) >> 5) << (IPC_FIFO_MSG_CHANNEL_BITS + 3)) |
        (GBA_SAVE_IPC_CMD_SETUP << IPC_FIFO_MSG_CHANNEL_BITS) |
        IPC_CHANNEL_GBA_SAVE);
    while (ipc_isRecvFifoEmpty());
    ipc_recvWordDirect();
}

extern "C" u8 sav_readSaveByteFromFile(u32 saveAddress)
{
    vm_enableNestedIrqs();
    // Default to the unprogrammed-save value so that a read past the end of the
    // save file (or a failed f_read) never returns stack garbage. A garbage byte
    // could make a game's verify loop spin or report a corrupted save.
    u8 saveByte = SAVE_DATA_FILL;
    if (Environment::IsIsNitroEmulator())
    {
        // save buffer in extended memory
        saveByte = ISNITRO_SAVE_BUFFER[saveAddress];
    }
    else
    {
        // write to file
        f_lseek(&gSaveFile, saveAddress);
        UINT bytesRead = 0;
        if (f_read(&gSaveFile, &saveByte, 1, &bytesRead) != FR_OK || bytesRead != 1)
        {
            saveByte = SAVE_DATA_FILL;
        }
    }
    vm_disableNestedIrqs();
    return saveByte;
}

extern "C" void sav_writeSaveByteToFile(u32 saveAddress, u8 data)
{
    vm_enableNestedIrqs();
    if (Environment::IsIsNitroEmulator())
    {
        // save buffer in extended memory
        ISNITRO_SAVE_BUFFER[saveAddress] = data;
    }
    else
    {
        // write to file
        f_lseek(&gSaveFile, saveAddress);
        UINT bytesWritten = 0;
        f_write(&gSaveFile, &data, 1, &bytesWritten);
    }
    vm_disableNestedIrqs();
}

extern "C" void sav_flushSaveFile(void)
{
    vm_enableNestedIrqs();
    if (!Environment::IsIsNitroEmulator())
    {
        f_sync(&gSaveFile);
    }
    vm_disableNestedIrqs();
}

extern "C" void sav_writeSaveToFile(void)
{
    if (gGbaSaveShared.saveDataSize != 0 && !Environment::IsIsNitroEmulator())
    {
        f_lseek(&gSaveFile, 0);
        UINT bytesWritten = 0;
        f_write(&gSaveFile, gSaveData, gGbaSaveShared.saveDataSize, &bytesWritten);
        f_sync(&gSaveFile);
    }

    gGbaSaveShared.saveState = GBA_SAVE_STATE_CLEAN;
    emu_vblankIrqSkipSaveCheckInstruction = sSkipSaveCheckInstruction;
}

namespace
{
/// @brief A per-game save type override for titles whose SDK save tag is
///        missing or ambiguous (e.g. 4K/64K EEPROM games and Classic NES).
struct GameIdSaveOverride
{
    u32 gameCode;
    u16 type;
    u32 size;
};

/// @brief Maps a save type to its SDK patch function.
bool (*getSavePatchFunction(u16 type))(const SaveTypeInfo*, FIL*, u32, u8*)
{
    switch (type)
    {
        case SAVE_TYPE_EEPROM_V111: return eeprom_patchV111;
        case SAVE_TYPE_EEPROM_V120:
        case SAVE_TYPE_EEPROM_V121:
        case SAVE_TYPE_EEPROM_V122: return eeprom_patchV120;
        case SAVE_TYPE_EEPROM_V124:
        case SAVE_TYPE_EEPROM_V125: return eeprom_patchV124;
        case SAVE_TYPE_EEPROM_V126: return eeprom_patchV126;
        case SAVE_TYPE_FLASH_V120:
        case SAVE_TYPE_FLASH_V121: return flash_patchV120;
        case SAVE_TYPE_FLASH_V123:
        case SAVE_TYPE_FLASH_V124:
        case SAVE_TYPE_FLASH_V125: return flash_patchV123;
        case SAVE_TYPE_FLASH_V126: return flash_patchV126;
        case SAVE_TYPE_FLASH512_V130:
        case SAVE_TYPE_FLASH512_V131:
        case SAVE_TYPE_FLASH512_V133: return flash_patch512V130;
        case SAVE_TYPE_FLASH1M_V102: return flash_patch1MV102;
        case SAVE_TYPE_FLASH1M_V103: return flash_patch1MV103;
        case SAVE_TYPE_SRAM_V110: return sram_patchV110;
        case SAVE_TYPE_SRAM_V111:
        case SAVE_TYPE_SRAM_V112:
        case SAVE_TYPE_SRAM_V113: return sram_patchV111;
        default: return nullptr;
    }
}
}

// Encodes the four character game code from the rom header (stored little
// endian at 0xAC) into the u32 used by the override table.
#define GAME_ID(a, b, c, d) \
    ((u32)(a) | ((u32)(b) << 8) | ((u32)(c) << 16) | ((u32)(d) << 24))

/// @brief Compact game id override table. Placed in .ewram (loaded from vramab,
///        like sSaveTypeInfos) rather than .rodata so the chronically tight
///        128 KB vrama region keeps headroom. The save types and sizes are
///        derived from the mGBA cartridge override list, which is the community
///        reference for titles whose SDK save tags are missing or ambiguous.
[[gnu::section(".ewram")]]
static const GameIdSaveOverride sGameIdSaveOverrides[] =
{
    // 64 Kbit (8 KB) EEPROM titles.
    {GAME_ID('U','3','I','J'), SAVE_TYPE_EEPROM_V124, 8 * 1024}, // Boktai
    {GAME_ID('U','3','I','E'), SAVE_TYPE_EEPROM_V124, 8 * 1024},
    {GAME_ID('U','3','I','P'), SAVE_TYPE_EEPROM_V124, 8 * 1024},
    {GAME_ID('U','3','2','J'), SAVE_TYPE_EEPROM_V124, 8 * 1024}, // Boktai 2
    {GAME_ID('U','3','2','E'), SAVE_TYPE_EEPROM_V124, 8 * 1024},
    {GAME_ID('U','3','2','P'), SAVE_TYPE_EEPROM_V124, 8 * 1024},
    {GAME_ID('U','3','3','J'), SAVE_TYPE_EEPROM_V124, 8 * 1024}, // Shin Bokura no Taiyou
    {GAME_ID('A','C','8','J'), SAVE_TYPE_EEPROM_V124, 8 * 1024}, // Crash Bandicoot 2
    {GAME_ID('A','C','8','E'), SAVE_TYPE_EEPROM_V124, 8 * 1024},
    {GAME_ID('A','C','8','P'), SAVE_TYPE_EEPROM_V124, 8 * 1024},
    {GAME_ID('B','D','K','J'), SAVE_TYPE_EEPROM_V124, 8 * 1024}, // DigiCommunication Nyo
    {GAME_ID('A','L','G','P'), SAVE_TYPE_EEPROM_V124, 8 * 1024}, // Dragon Ball Z: LoG
    {GAME_ID('A','L','F','J'), SAVE_TYPE_EEPROM_V124, 8 * 1024}, // Dragon Ball Z: LoG II
    {GAME_ID('A','L','F','E'), SAVE_TYPE_EEPROM_V124, 8 * 1024},
    {GAME_ID('A','L','F','P'), SAVE_TYPE_EEPROM_V124, 8 * 1024},
    {GAME_ID('B','D','B','E'), SAVE_TYPE_EEPROM_V124, 8 * 1024}, // Dragon Ball Z: Taiketsu
    {GAME_ID('B','D','B','P'), SAVE_TYPE_EEPROM_V124, 8 * 1024},
    {GAME_ID('K','H','P','J'), SAVE_TYPE_EEPROM_V124, 8 * 1024}, // Koro Koro Puzzle
    {GAME_ID('B','S','M','E'), SAVE_TYPE_EEPROM_V124, 8 * 1024}, // Metal Slug Advance
    {GAME_ID('A','R','8','E'), SAVE_TYPE_EEPROM_V124, 8 * 1024}, // Rocky
    {GAME_ID('A','R','O','P'), SAVE_TYPE_EEPROM_V124, 8 * 1024},
    {GAME_ID('A','A','2','J'), SAVE_TYPE_EEPROM_V124, 8 * 1024}, // Super Mario Advance 2
    {GAME_ID('A','A','2','E'), SAVE_TYPE_EEPROM_V124, 8 * 1024},
    {GAME_ID('A','A','2','P'), SAVE_TYPE_EEPROM_V124, 8 * 1024},
    {GAME_ID('A','3','A','J'), SAVE_TYPE_EEPROM_V124, 8 * 1024}, // Super Mario Advance 3
    {GAME_ID('A','3','A','E'), SAVE_TYPE_EEPROM_V124, 8 * 1024},
    {GAME_ID('A','3','A','P'), SAVE_TYPE_EEPROM_V124, 8 * 1024},
    {GAME_ID('A','L','U','E'), SAVE_TYPE_EEPROM_V124, 8 * 1024}, // Super Monkey Ball Jr.
    {GAME_ID('A','L','U','P'), SAVE_TYPE_EEPROM_V124, 8 * 1024},
    {GAME_ID('B','U','H','J'), SAVE_TYPE_EEPROM_V124, 8 * 1024}, // Ueki no Housoku
    {GAME_ID('K','Y','G','J'), SAVE_TYPE_EEPROM_V124, 8 * 1024}, // Yoshi's Universal Gravitation
    {GAME_ID('K','Y','G','E'), SAVE_TYPE_EEPROM_V124, 8 * 1024},
    {GAME_ID('K','Y','G','P'), SAVE_TYPE_EEPROM_V124, 8 * 1024},
    {GAME_ID('T','C','H','K'), SAVE_TYPE_EEPROM_V124, 8 * 1024}, // Aging cartridge

    // 128 KB (1 Mbit) flash titles. mGBA force-selects FLASH1M for these, so a
    // stripped/hacked rom with no usable SDK tag still gets the right save type
    // instead of silently falling back to 32 KB SRAM. For a clean rom the
    // detected FLASH1M_V1xx tag is authoritative and is preserved (only the size
    // is corrected), so the version chosen here does not override it.
    {GAME_ID('A','X','4','J'), SAVE_TYPE_FLASH1M_V103, 128 * 1024}, // Super Mario Advance 4
    {GAME_ID('A','X','4','E'), SAVE_TYPE_FLASH1M_V103, 128 * 1024},
    {GAME_ID('A','X','4','P'), SAVE_TYPE_FLASH1M_V103, 128 * 1024},

    // Titles that contain save tags but must not use a save chip, otherwise
    // they lock up or trigger anti-piracy checks.
    {GAME_ID('A','I','2','E'), SAVE_TYPE_NONE, 0}, // Iridion II
    {GAME_ID('A','I','2','P'), SAVE_TYPE_NONE, 0},
    {GAME_ID('A','2','Y','E'), SAVE_TYPE_NONE, 0}, // Top Gun - Combat Zones
    {GAME_ID('A','S','L','E'), SAVE_TYPE_NONE, 0}, // Stuart Little 2
    {GAME_ID('A','S','L','F'), SAVE_TYPE_NONE, 0},
};
#undef GAME_ID

const SaveTypeInfo* sav_resolveGameIdSaveTypeInfo(
    u32 gameCode, const SaveTypeInfo* detected, SaveTypeInfo& storage)
{
    const GameIdSaveOverride* override = nullptr;
    for (const auto& entry : sGameIdSaveOverrides)
    {
        if (entry.gameCode == gameCode)
        {
            override = &entry;
            break;
        }
    }

    if (!override && (gameCode & 0xFF) == 'F')
    {
        // Classic NES Series (Famicom Mini) ships without a reliable save tag.
        storage = SaveTypeInfo { "", 0, SAVE_TYPE_EEPROM_V124, 8 * 1024, eeprom_patchV124 };
        return &storage;
    }

    if (!override)
    {
        return detected;
    }

    if (override->type == SAVE_TYPE_NONE)
    {
        storage = SaveTypeInfo { "", 0, SAVE_TYPE_NONE, 0, nullptr };
        return &storage;
    }

    const u16 category = override->type & static_cast<u16>(SAVE_TYPE_MASK);
    if (detected && (detected->type & SAVE_TYPE_MASK) == category)
    {
        // The SDK patch version already found is authoritative; only correct
        // the save size (e.g. 4 Kbit vs 64 Kbit EEPROM).
        storage = *detected;
        if (override->size != 0)
        {
            storage.size = override->size;
        }
        return &storage;
    }

    storage = SaveTypeInfo
    {
        "", 0, static_cast<SaveType>(override->type), override->size,
        getSavePatchFunction(override->type)
    };
    return &storage;
}

void sav_getSaveTypeDescription(const SaveTypeInfo* saveTypeInfo, char* out)
{
    if (!saveTypeInfo)
    {
        // Untagged roms fall back to the 32 KB SRAM cache.
        strcpy(out, "SRAM 32K");
        return;
    }

    switch (saveTypeInfo->type & SAVE_TYPE_MASK)
    {
        case SAVE_TYPE_EEPROM:
            strcpy(out, saveTypeInfo->size <= 512 ? "EEPROM 512B" : "EEPROM 8K");
            break;
        case SAVE_TYPE_FLASH:
            // SAVE_TYPE_FLASH1M sets bit 13 over SAVE_TYPE_FLASH.
            strcpy(out, (saveTypeInfo->type & (1u << 13)) ? "FLASH 128K" : "FLASH 64K");
            break;
        case SAVE_TYPE_SRAM:
            strcpy(out, "SRAM 32K");
            break;
        default:
            strcpy(out, "NONE");
            break;
    }
}
