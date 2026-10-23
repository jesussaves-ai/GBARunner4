#pragma once
#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Runs the in-game menu. Called from the ARM9 vblank handler while
///        L + R + Start are held; the guest stays frozen until this returns.
///        Lives in cold .text (not ITCM).
void igm_run(void);

#ifdef __cplusplus
}
#endif
