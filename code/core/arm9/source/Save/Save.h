#pragma once
#include "Fat/ff.h"
#include "SaveTypeInfo.h"
#include "GbaSaveShared.h"

#define SAVE_DATA_FILL              0xFF
#define SAVE_DATA_SIZE              (32 * 1024)
#define ISNITRO_SAVE_BUFFER         ((vu8*)0x02480000)
#define ISNITRO_SAVE_BUFFER_SIZE    (128 * 1024)

extern u8 gSaveData[SAVE_DATA_SIZE];
extern FIL gSaveFile;
extern gba_save_shared_t gGbaSaveShared;

extern u32 emu_vblankIrqSkipSaveCheckInstruction;

bool sav_tryPatchFunction(const u32* signature, u32 saveSwiNumber, void* patchFunction);

/// @brief Writes a Thumb SWI to the ROM function located at the given GBA
///        address. Handles both functions in the linearly mapped 2 MB window
///        and functions in the paged high rom, so callers never need to know
///        where in the rom a function lives.
/// @param gbaAddress The GBA address of the function to patch. The bottom bit
///        is ignored.
/// @param swiNumber The save SWI number to write.
void sav_patchRomFunction(u32 gbaAddress, u32 swiNumber);

/// @brief Resolves the save type info to use for a game, applying the compact
///        game id override table for titles with missing or ambiguous SDK save
///        tags (e.g. 4K/64K EEPROM games and the Classic NES series).
/// @param gameCode The four character game code from the rom header.
/// @param detected The save type info found by scanning the SDK tag, or
///        nullptr if none was found.
/// @param storage Scratch storage used when an override needs to return a
///        synthesized or adjusted save type info. Must outlive the result.
/// @return The effective save type info. May equal `detected` when no override
///        applies, be `&storage` when one does, or be nullptr when neither is
///        available.
const SaveTypeInfo* sav_resolveGameIdSaveTypeInfo(
    u32 gameCode, const SaveTypeInfo* detected, SaveTypeInfo& storage);

/// @brief Writes a short, uppercase description of a resolved save type for the
///        boot diagnostics badge (e.g. "FLASH 128K", "EEPROM 512B", "NONE").
/// @param saveTypeInfo The resolved save type info, or nullptr for the default
///        32 KB SRAM fallback used for untagged roms.
/// @param out Destination buffer of at least 16 bytes.
void sav_getSaveTypeDescription(const SaveTypeInfo* saveTypeInfo, char* out);

void sav_initializeSave(const SaveTypeInfo* saveTypeInfo, const char* savePath);

#ifdef __cplusplus
extern "C" {
#endif

u8 sav_readSaveByteFromFile(u32 offset);
void sav_writeSaveByteToFile(u32 offset, u8 data);
void sav_flushSaveFile(void);
void sav_writeSaveToFile(void);

#ifdef __cplusplus
}
#endif
