#pragma once
#include <nds/ndstypes.h>
#include <stddef.h>

typedef u16 bool16;

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Returns 1 when the console exposes the full DSi/TWL 16 MB main RAM
///        (SCFG_EXT main RAM limit >= 16 MB). C-callable form of
///        Environment::HasExtendedMainMemory() for the C translation units.
int env_hasExtendedMainMemory(void);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

#include "Logger/ILogger.h"
#include "Core/Heap/GlobalHeap.h"

extern ILogger* gLogger;

#endif
