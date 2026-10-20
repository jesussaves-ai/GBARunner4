#include "common.h"

/// @brief Contains the original GBA color palette values without correction applied.
///        This is required to ensure palette reads return the expected value.
///        Placed in .ewram.bss because vrama (the default .bss region) is tight.
[[gnu::section(".ewram.bss")]]
u16 gShadowPalette[512];
