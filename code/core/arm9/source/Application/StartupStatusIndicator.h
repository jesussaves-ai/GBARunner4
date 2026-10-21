#pragma once
#include "common.h"

/// @brief Renders a small, self-clearing boot status badge on the sub screen so
///        the active TWL/NTR configuration (134 MHz ARM9 clock and the size of
///        the active SdCache pool) is visible while the ROM loads.
///
/// The badge uses the sub BG0 text layer and is disabled again before the GBA
/// display/border are configured, so it never interferes with game assets or a
/// custom border. All of this is cold initialization code and costs zero .itcm.
class StartupStatusIndicator
{
public:
    /// @brief Renders the badge for the current hardware configuration.
    ///        Reads Environment::IsDsiMode(), the SCFG clock register and
    ///        Environment::HasExtendedMainMemory() itself.
    static void Show();

    /// @brief Clears the badge tilemap and disables the status BG0 layer.
    static void Clear();
};
