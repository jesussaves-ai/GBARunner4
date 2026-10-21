#include "common.h"
#include <cstddef>
#include "RomGpioRtc.h"
#include "RomGpioLightSensor.h"
#include "RomGpio.h"

[[gnu::section(".ewram.bss")]]
RomGpio gRomGpio;
[[gnu::section(".ewram.bss")]]
static RomGpioRtc sRomGpioRtc;
[[gnu::section(".ewram.bss")]]
static RomGpioLightSensor sRomGpioLightSensor;

void RomGpio::Initialize(rio_registers_t* romGpioRegisters)
{
    _registers = romGpioRegisters;
    _registersRomData = *romGpioRegisters;
    _lightSensorEnabled = false;
    sRomGpioLightSensor.Reset();
    Reset();
}

void RomGpio::Reset()
{
    _inputData = 0;
    _outputData = 0;
    _direction = 0;
    _control = RIO_CONTROL_READ_DISABLE;
    UpdateRomRegisters();
}

void RomGpio::UpdateRomRegisters()
{
    if (_control == RIO_CONTROL_READ_DISABLE)
    {
        // When reading of the registers is disabled, the original rom data is read.
        *_registers = _registersRomData;
    }
    else
    {
        _registers->data = GetGpioState();
        _registers->direction = _direction;
        _registers->control = RIO_CONTROL_READ_ENABLE;
    }
}

static void updateRomGpioPeripherals()
{
    sRomGpioRtc.Update(gRomGpio);
    if (gRomGpio.IsLightSensorEnabled())
        sRomGpioLightSensor.Update(gRomGpio);
}

// Boktai sunlight-level hotkey: Select + D-Pad Left/Right steps the level. Read
// here (not from a per-frame hook) because rio_write already runs whenever the
// game polls the GPIO, so it costs zero .itcm.
static bool sLightLevelHotkeyHeld = false;

static void updateRomGpioLightLevelHotkey()
{
    constexpr u16 selectMask = 1 << 2;
    constexpr u16 leftMask = 1 << 4;
    constexpr u16 rightMask = 1 << 5;

    // GBA/DS REG_KEYINPUT is active LOW.
    u16 keys = *(volatile u16*)0x04000130;
    int delta = 0;
    if ((keys & selectMask) == 0)
    {
        if ((keys & leftMask) == 0)
            delta = -1;
        else if ((keys & rightMask) == 0)
            delta = 1;
    }
    if (delta != 0 && !sLightLevelHotkeyHeld)
        sRomGpioLightSensor.StepLevel(delta);
    sLightLevelHotkeyHeld = (delta != 0);
}

extern "C" void rio_write(u32 offset, u16 value)
{
    switch (offset)
    {
        case offsetof(rio_registers_t, data):
        {
            gRomGpio.WriteDataRegister(value);
            updateRomGpioPeripherals();
            break;
        }
        case offsetof(rio_registers_t, direction):
        {
            gRomGpio.WriteDirectionRegister(value);
            updateRomGpioPeripherals();
            break;
        }
        case offsetof(rio_registers_t, control):
        {
            gRomGpio.WriteControlRegister(value);
            break;
        }
    }

    if (gRomGpio.IsLightSensorEnabled())
        updateRomGpioLightLevelHotkey();
}
