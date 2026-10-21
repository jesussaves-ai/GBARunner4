#pragma once
#include "common.h"

class RomGpio;

/// @brief Boktai solar (light) sensor, modelled after mGBA's light sensor. It
///        shares the RTC GPIO pins at 0x080000C4: CS (pin 2) selects the chip,
///        RST (pin 1) resets the counter, a CLK (pin 0) rising edge advances it,
///        and pin 3 outputs 1 once the counter reaches the current sunlight
///        level. Games pulse CLK and time how long the output stays 0.
class RomGpioLightSensor
{
public:
    RomGpioLightSensor() : _counter(0), _edge(false), _level(DEFAULT_LEVEL) { }

    void Reset();

    /// @brief Advances the sensor from the current GPIO pin levels.
    void Update(RomGpio& romGpio);

    /// @brief Steps the emulated sunlight level, clamped to 0 (dark)..9 (max).
    void StepLevel(int delta);

    u8 GetLevel() const { return _level; }

private:
    /// @brief Index 5 (luminance ~62) of mGBA's table: a playable mid-day sun
    ///        that lets solar weapons recharge without overheating.
    static constexpr u8 DEFAULT_LEVEL = 5;

    u16 _counter;
    bool _edge;
    u8 _level;
};
