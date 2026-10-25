#ifdef GBAR3_SCREENSHOT

// Test-build helper: after the guest has run for a while, capture the DS main
// engine (which is showing the GBA screen) into VRAM bank C and write it to the
// SD card as /_gba/result.bmp, so a test ROM's on-screen result can be read off
// the card instead of the DS screen.
//
// The capture is triggered from the VBlank IRQ (Emulator/VBlankIrq.s), mirroring
// how the save pipeline already writes to SD from VBlank. That IRQ stack is tiny
// (288 bytes), so all buffers live in .bss and the files are opened up front by
// test_screenshotInit() from the main thread; the IRQ path only does
// f_lseek/f_write/f_sync, exactly like sav_writeSaveToFile().

#include "common.h"
#include <string.h>
#include <libtwl/gfx/gfx.h>
#include "mini-printf.h"
#include "Fat/File.h"

// Number of guest frames to run before capturing. The bundled GBA test ROMs
// finish drawing well within this, and it leaves room for a boot splash.
#define SCREENSHOT_FRAME_COUNT  600

#define SCREEN_WIDTH    256
#define SCREEN_HEIGHT   192

// VRAM bank C is allocated to the LCDC in main.cpp (mem_setVramCMapping), so it
// is free to use as a capture destination and CPU-readable at its LCDC address
// (GBATEK: VRAM C, MST=0 -> 0x06840000..0x0685FFFF).
#define VRAM_C_LCDC_ADDR    0x06840000u

// DISPCAPCNT: enable | source A | size 256x192 | VRAM write block C (block 2),
// write offset 0.
#define DISPCAPCNT_CAPTURE  0x80320000u

#define RESULT_BMP_PATH     "/_gba/result.bmp"
#define RESULT_TXT_PATH     "/_gba/result.txt"

static File sBmpFile;
static File sTxtFile;
static bool sBmpReady;
static bool sTxtReady;
static u32  sFrame;
static u32  sState;

// All working buffers are static: the IRQ stack has no room for them.
static u8   sBmpHeader[54];
static u8   sBmpRow[SCREEN_WIDTH * 3];
static char sTxt[192];

static void putLe32(u8* p, u32 v)
{
    p[0] = v & 0xFF;
    p[1] = (v >> 8) & 0xFF;
    p[2] = (v >> 16) & 0xFF;
    p[3] = (v >> 24) & 0xFF;
}

static char* appendStr(char* dst, const char* src)
{
    while (*src)
        *dst++ = *src++;
    return dst;
}

static void buildBmpHeader(void)
{
    const u32 imageBytes = (SCREEN_WIDTH * 3) * SCREEN_HEIGHT;
    memset(sBmpHeader, 0, sizeof(sBmpHeader));
    sBmpHeader[0] = 'B';
    sBmpHeader[1] = 'M';
    putLe32(sBmpHeader + 2, 54 + imageBytes);   // file size
    putLe32(sBmpHeader + 10, 54);               // pixel data offset
    putLe32(sBmpHeader + 14, 40);               // BITMAPINFOHEADER size
    putLe32(sBmpHeader + 18, SCREEN_WIDTH);
    putLe32(sBmpHeader + 22, SCREEN_HEIGHT);
    sBmpHeader[26] = 1;                         // planes
    sBmpHeader[28] = 24;                        // bits per pixel
    putLe32(sBmpHeader + 34, imageBytes);       // image size
    putLe32(sBmpHeader + 38, 2835);             // x pixels per meter
    putLe32(sBmpHeader + 42, 2835);             // y pixels per meter
}

/// @brief Opens the output files from the main thread (large stack) so the IRQ
///        path never has to call f_open.
extern "C" void test_screenshotInit(void)
{
    f_mkdir("/_gba");   // ensure the output directory exists (ignore result)
    sBmpReady = (sBmpFile.Open(RESULT_BMP_PATH, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK);
    sTxtReady = (sTxtFile.Open(RESULT_TXT_PATH, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK);
}

static void writeScreenshot(void)
{
    u32 written;
    sBmpFile.Seek(0);
    sBmpFile.Write(sBmpHeader, sizeof(sBmpHeader), written);

    const volatile u16* vram = (const volatile u16*)VRAM_C_LCDC_ADDR;

    // BMP rows are stored bottom-up. DS color is 0BBBBBGGGGGRRRRR.
    for (int y = SCREEN_HEIGHT - 1; y >= 0; y--)
    {
        const volatile u16* src = vram + y * SCREEN_WIDTH;
        for (int x = 0; x < SCREEN_WIDTH; x++)
        {
            u16 c = src[x];
            u8 r5 = c & 0x1F;
            u8 g5 = (c >> 5) & 0x1F;
            u8 b5 = (c >> 10) & 0x1F;
            sBmpRow[x * 3 + 0] = (u8)((b5 << 3) | (b5 >> 2));
            sBmpRow[x * 3 + 1] = (u8)((g5 << 3) | (g5 >> 2));
            sBmpRow[x * 3 + 2] = (u8)((r5 << 3) | (r5 >> 2));
        }
        sBmpFile.Write(sBmpRow, sizeof(sBmpRow), written);
    }
    sBmpFile.Sync();
}

static void writeDiagnostics(u32 capcnt)
{
    char* p = appendStr(sTxt, "GBARunner4 screenshot build\nframe=");
    p += mini_snprintf(p, 24, "%u", sFrame);
    p = appendStr(p, "\ndispcapcnt=");
    p += mini_snprintf(p, 24, "%08x", capcnt);
    p = appendStr(p, "\nvramC[0]=");
    p += mini_snprintf(p, 24, "%04x", *(volatile u16*)VRAM_C_LCDC_ADDR);
    p = appendStr(p, "\nbmp=");
    p += mini_snprintf(p, 24, "%d", sBmpReady ? 1 : 0);
    *p++ = '\n';

    u32 written;
    sTxtFile.Seek(0);
    sTxtFile.Write(sTxt, (u32)(p - sTxt), written);
    sTxtFile.Sync();
}

static void captureDone(void)
{
    u32 capcnt = REG_DISPCAPCNT;
    REG_DISPCAPCNT = 0;
    if (sBmpReady)
        writeScreenshot();
    if (sTxtReady)
        writeDiagnostics(capcnt);
}

extern "C" void test_vblankTick(void)
{
    sFrame++;

    if (sState == 0)
    {
        if (sFrame >= SCREENSHOT_FRAME_COUNT)
        {
            // Arm the capture; it runs over the next frame and completes at
            // line 192, i.e. by the next VBlank.
            REG_DISPCAPCNT = DISPCAPCNT_CAPTURE;
            sState = 1;
        }
        return;
    }

    if (sState == 1 && sFrame >= SCREENSHOT_FRAME_COUNT + 2)
    {
        captureDone();
        sState = 2;
    }
}

#else

// Keep the symbol available to main.cpp regardless of the build flavour.
extern "C" void test_screenshotInit(void) { }

#endif // GBAR3_SCREENSHOT
