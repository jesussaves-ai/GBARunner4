#include "common.h"
#include "RomGpio.h"
#include "RomGpioLightSensor.h"

// mGBA's GBA_LUX_LEVELS: a higher index is a brighter sun.
static const u16 sLuxLevels[10] = { 5, 11, 18, 27, 42, 62, 84, 109, 139, 183 };

#define RIO_PIN_CLK     0
#define RIO_PIN_RST     1
#define RIO_PIN_CS      2
#define RIO_PIN_DATA    3

void RomGpioLightSensor::Reset()
{
    _counter = 0;
    _edge = false;
    _level = DEFAULT_LEVEL;
}

void RomGpioLightSensor::Update(RomGpio& romGpio)
{
    if (romGpio.GetPinState(RIO_PIN_CS))
        return; // chip not selected

    if (romGpio.GetPinState(RIO_PIN_RST))
    {
        _counter = 0;
        _edge = true;
    }
    if (romGpio.GetPinState(RIO_PIN_CLK) && _edge)
        _counter++;
    _edge = !romGpio.GetPinState(RIO_PIN_CLK);

    bool output = _counter >= sLuxLevels[_level];
    romGpio.SetPinState(RIO_PIN_DATA, output);
}

void RomGpioLightSensor::StepLevel(int delta)
{
    int level = (int)_level + delta;
    if (level < 0)
        level = 0;
    else if (level > 9)
        level = 9;
    _level = (u8)level;
}
