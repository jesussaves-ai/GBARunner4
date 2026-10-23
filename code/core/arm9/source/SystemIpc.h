#pragma once

void sysipc_setTopBacklight(bool enabled);
void sysipc_setBottomBacklight(bool enabled);

/// @brief Asks the ARM7 to warm-reset the console (it flushes a dirty save
///        first). Never returns from the ARM9's point of view for long: the
///        whole system reboots.
void sysipc_requestReset(void);

/// @brief Asks the ARM7 to power the console off (after flushing a dirty save).
void sysipc_requestPowerOff(void);
