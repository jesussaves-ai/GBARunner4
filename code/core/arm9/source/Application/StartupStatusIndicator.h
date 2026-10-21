#pragma once
#include "common.h"

/// @brief Renders a small, self-clearing boot status badge on the sub screen so
///        the active TWL/NTR configuration (134 MHz ARM9 clock and the size of
///        the active SdCache pool), plus the rom game code and the resolved
///        save type, are visible while the ROM loads.
///
/// The badge uses the sub BG0 text layer and is disabled again before the GBA
/// display/border are configured, so it never interferes with game assets or a
/// custom border. All of this is cold initialization code and costs zero .itcm.
class StartupStatusIndicator
{
public:
    /// @brief Renders the environment line (TWL/NTR, clock, cache) and prepares
    ///        the font and BG0 layer. Reads Environment::IsDsiMode(), the SCFG
    ///        clock register and Environment::HasExtendedMainMemory() itself.
    static void Show();

    /// @brief Renders the "ID: <game code> | SAVE: <type>" line. Must be called
    ///        after Show() and before Clear(), once the rom header and the
    ///        resolved save type are known.
    /// @param gameCode The four character rom game code (header offset 0xAC).
    /// @param saveTypeDescription A short description of the save type, e.g.
    ///        "FLASH 128K". Only A-Z, 0-9, space and ':|-/' are rendered.
    static void ShowGameInfo(u32 gameCode, const char* saveTypeDescription);

    /// @brief Clears the badge tilemap and disables the status BG0 layer.
    static void Clear();
};
