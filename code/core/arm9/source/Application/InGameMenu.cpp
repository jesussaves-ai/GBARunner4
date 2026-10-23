#include "common.h"
#include <libtwl/gfx/gfx.h>
#include "StartupStatusIndicator.h"
#include "SystemIpc.h"
#include "InGameMenu.h"

// In-game pause menu, opened by L + R + Start (Select released). It is invoked
// from the ARM9 vblank handler, so the guest is frozen for as long as it runs:
// no new vblank is serviced until this returns. Rendering reuses the boot
// badge's sub BG0 overlay (StartupStatusIndicator), which maps VRAM bank C to
// the sub engine and hands it back to LCDC on Clear().
//
// DS REG_KEYINPUT (0x04000130) is active LOW and holds the ten main buttons:
//   bit0 A, 1 B, 2 Select, 3 Start, 4 Right, 5 Left, 6 Up, 7 Down, 8 R, 9 L.

#define IGM_TRIGGER_LRSTART 0x0308u // L | R | Start
#define IGM_SELECT          0x0004u

#define IGM_KEY_A       (1u << 0)
#define IGM_KEY_UP      (1u << 6)
#define IGM_KEY_DOWN    (1u << 7)
#define IGM_KEY_B       (1u << 1)
#define IGM_KEY_START   (1u << 3)

namespace
{
enum class MenuAction
{
    Resume,
    Restart,
    Shutdown
};

const char* const kItems[] = { "RESUME", "RESTART", "SHUT DOWN" };
constexpr u32 kItemCount = sizeof(kItems) / sizeof(kItems[0]);

constexpr u32 kTitleRow = 9;
constexpr u32 kFirstItemRow = 12;
constexpr u32 kRowStep = 2;

u16 readKeys()
{
    return *(volatile u16*)0x04000130;
}

/// @brief True when the given key bit is currently held (active low).
bool isPressed(u16 keys, u16 mask)
{
    return (keys & mask) == 0;
}

/// @brief Blocks until the given keys are all released.
void waitRelease(u16 mask)
{
    while ((readKeys() & mask) != mask) {}
}

/// @brief Blocks until the start of the next vblank.
void waitNextFrame()
{
    vu16* vcount = (vu16*)0x04000006;
    while (*vcount >= 192) {} // wait for the current vblank to end
    while (*vcount < 192) {}  // wait for the next vblank to begin
}

void drawMenu(u32 selected)
{
    StartupStatusIndicator::DrawTextRow(kTitleRow, "PAUSED", false);
    for (u32 i = 0; i < kItemCount; i++)
        StartupStatusIndicator::DrawTextRow(kFirstItemRow + i * kRowStep, kItems[i], i == selected);
}
}

extern "C" void igm_run(void)
{
    // Save the sub engine config so the border / captured game come back
    // exactly as they were once the menu closes.
    const u32 savedDispCntSub = REG_DISPCNT_SUB;

    StartupStatusIndicator::BeginOverlay();

    u32 selected = 0;
    drawMenu(selected);

    // Do not let the press that opened the menu immediately act on an item.
    waitRelease(IGM_TRIGGER_LRSTART);

    bool resume = false;
    while (!resume)
    {
        waitNextFrame();

        u16 keys = readKeys();
        if (isPressed(keys, IGM_KEY_UP))
        {
            selected = (selected == 0) ? kItemCount - 1 : selected - 1;
            drawMenu(selected);
            waitRelease(IGM_KEY_UP);
        }
        else if (isPressed(keys, IGM_KEY_DOWN))
        {
            selected = (selected + 1) % kItemCount;
            drawMenu(selected);
            waitRelease(IGM_KEY_DOWN);
        }

        if (isPressed(keys, IGM_KEY_A))
        {
            switch ((MenuAction)selected)
            {
                case MenuAction::Resume:
                    resume = true;
                    break;

                case MenuAction::Restart:
                    StartupStatusIndicator::Clear();
                    sysipc_requestReset();
                    while (true) {} // the ARM7 warm-resets the console
                    break;

                case MenuAction::Shutdown:
                    StartupStatusIndicator::Clear();
                    sysipc_requestPowerOff();
                    while (true) {}
                    break;
            }
        }
        else if (isPressed(keys, IGM_KEY_B) || isPressed(keys, IGM_KEY_START))
        {
            resume = true; // quick resume
        }
    }

    // Give the sub screen back to the border/capture path before the guest runs.
    StartupStatusIndicator::Clear();
    REG_DISPCNT_SUB = savedDispCntSub;
}
