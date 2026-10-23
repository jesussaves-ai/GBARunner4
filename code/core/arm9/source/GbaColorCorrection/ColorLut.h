#pragma once
#include "ColorProfiles.h"

#define COLOR_LUT_SIZE      (1 << 15)

/// @brief Color-correction lookup table. Lazily allocated (heap) only while
///        color correction is enabled; NULL by default. The palette store
///        handlers branch on gColorCorrectionEnabled and only dereference this
///        pointer when correction is active, so the 64 KB table costs no static
///        RAM in the common (correction off) case.
extern u16* gColorLut;

/// @brief Non-zero while the palette store handlers must apply color correction.
extern "C" u8 gColorCorrectionEnabled;

void clut_initColorCorrection(const ColorProfile* preset, int gammaIndex);
void clut_disableColorCorrection();
