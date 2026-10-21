#include "common.h"
#include <string.h>
#include <libtwl/gfx/gfx.h>
#include <libtwl/gfx/gfxBackground.h>
#include <libtwl/gfx/gfxPalette.h>
#include <libtwl/mem/memVram.h>
#include "Core/Environment.h"
#include "StartupStatusIndicator.h"

// The badge is drawn with a 4bpp text BG0 on the sub engine. The tilemap and
// tile data are placed past the regions used by the border (BG2: tiles at
// offset 0, tilemap at 0x4000) and the center/mask layer (BG3: tilemap at
// 0x1000) so the badge never collides with them, and it is cleared before
// either is configured anyway.
//
// VRAM bank C sits at the sub BG window (0x06200000) only while it is mapped
// to the sub engine (VRAMCNT_C = MST 4). At boot main.cpp leaves it mapped to
// LCDC, which is required later so GbaBorderService can write the border into
// bank C at its LCDC address (0x06840000) before the capture double buffer
// remaps it. Show() therefore maps bank C to SUB_BG before touching it, and
// Clear() puts it back to LCDC so the border/capture path is unaffected.
#define STATUS_SUB_BG_BASE      0x06200000
#define STATUS_TILEMAP_OFFSET   0x8000          // screen base block 16
#define STATUS_TILES_OFFSET     0x14000         // char base block 5
#define STATUS_TILEMAP_ENTRIES  (32 * 32)
#define STATUS_TILE_HALFWORDS   16              // 4bpp 8x8 tile = 32 bytes

// Every cleared tilemap cell points at tile index 0, so index 0 MUST stay the
// empty tile. Glyphs are stored from index 1 onwards; if they started at 0 the
// first character of the string would be tiled across all 1,024 cells (the
// green 'F'/'T' grid). This was the actual root cause of the corruption.
#define STATUS_GLYPH_TILE_BASE  1

// priority 0, char base 5, screen base 16, 32x32, 4bpp
#define STATUS_BG0CNT_VALUE     ((16 << 8) | (5 << 2) | 0)

// Sub screen text row/colour.
#define STATUS_ROW              20
#define STATUS_BACKDROP_COLOR   0x0000
#define STATUS_TWL_COLOR        0x03E0          // green
#define STATUS_NTR_COLOR        0x4210          // light gray

#define REG_SCFG_CLK            (*(vu16*)0x04004004)
#define SCFG_CLK_CPU_SPEED      1

namespace
{
/// @brief One 5x7 glyph inside an 8x8 cell. Bit 6..2 of each row byte map to
///        the five glyph columns (bit 7 and bits 1..0 stay clear).
struct StatusGlyph
{
    char c;
    u8 rows[8];
};

// Only the characters used by the status strings are stored, keeping the table
// in .rodata and out of the precious ITCM.
constexpr StatusGlyph sGlyphs[] =
{
    { ' ', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
    { ':', { 0x00, 0x10, 0x10, 0x00, 0x10, 0x10, 0x00, 0x00 } },
    { '|', { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00 } },
    { '1', { 0x20, 0x60, 0x20, 0x20, 0x20, 0x20, 0x70, 0x00 } },
    { '3', { 0x70, 0x44, 0x04, 0x30, 0x04, 0x44, 0x70, 0x00 } },
    { '4', { 0x08, 0x18, 0x28, 0x48, 0x7C, 0x08, 0x08, 0x00 } },
    { '6', { 0x70, 0x40, 0x40, 0x78, 0x44, 0x44, 0x70, 0x00 } },
    { '7', { 0x7C, 0x08, 0x10, 0x10, 0x20, 0x20, 0x20, 0x00 } },
    { '8', { 0x70, 0x44, 0x44, 0x70, 0x44, 0x44, 0x70, 0x00 } },
    { 'A', { 0x70, 0x44, 0x44, 0x7C, 0x44, 0x44, 0x44, 0x00 } },
    { 'B', { 0x78, 0x44, 0x44, 0x78, 0x44, 0x44, 0x78, 0x00 } },
    { 'C', { 0x70, 0x44, 0x40, 0x40, 0x40, 0x44, 0x70, 0x00 } },
    { 'D', { 0x78, 0x44, 0x44, 0x44, 0x44, 0x44, 0x78, 0x00 } },
    { 'E', { 0x7C, 0x40, 0x40, 0x78, 0x40, 0x40, 0x7C, 0x00 } },
    { 'H', { 0x44, 0x44, 0x44, 0x7C, 0x44, 0x44, 0x44, 0x00 } },
    { 'L', { 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x7C, 0x00 } },
    { 'M', { 0x44, 0x6C, 0x54, 0x44, 0x44, 0x44, 0x44, 0x00 } },
    { 'N', { 0x44, 0x64, 0x54, 0x4C, 0x44, 0x44, 0x44, 0x00 } },
    { 'O', { 0x70, 0x44, 0x44, 0x44, 0x44, 0x44, 0x70, 0x00 } },
    { 'R', { 0x78, 0x44, 0x44, 0x78, 0x50, 0x48, 0x44, 0x00 } },
    { 'T', { 0x7C, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00 } },
    { 'W', { 0x44, 0x44, 0x44, 0x54, 0x54, 0x6C, 0x44, 0x00 } },
    { 'z', { 0x00, 0x00, 0x78, 0x08, 0x10, 0x20, 0x78, 0x00 } },
};

const u8* findGlyph(char c)
{
    for (const auto& glyph : sGlyphs)
    {
        if (glyph.c == c)
            return glyph.rows;
    }
    return nullptr;
}

/// @brief Expands an 8x8 1bpp glyph (or a blank tile when glyph is nullptr) into
///        a 4bpp tile, then stores it to VRAM using 16-bit writes only.
///
///        NDS VRAM silently drops 8-bit stores, so the tile is assembled in a
///        local RAM buffer and committed with u16 writes. Writing the raw bytes
///        through a u8*/memset pointer (as the original code did) leaves the
///        glyph memory unmodified, which is what produced the garbage tiles.
void writeGlyphTile(vu16* tileDest, const u8* glyph)
{
    u8 tile[STATUS_TILE_HALFWORDS * 2];
    memset(tile, 0, sizeof(tile));

    if (glyph)
    {
        for (u32 row = 0; row < 8; row++)
        {
            u8 bits = glyph[row];
            for (u32 col = 0; col < 8; col += 2)
            {
                u8 p0 = (bits >> (7 - col)) & 1;
                u8 p1 = (bits >> (7 - (col + 1))) & 1;
                tile[row * 4 + col / 2] = (p0 << 4) | p1;
            }
        }
    }

    for (u32 i = 0; i < STATUS_TILE_HALFWORDS; i++)
        tileDest[i] = (u16)(tile[i * 2] | (tile[i * 2 + 1] << 8));
}
}

void StartupStatusIndicator::Show()
{
    const bool twlMode = Environment::IsDsiMode();
    const bool clock134MHz = (REG_SCFG_CLK & SCFG_CLK_CPU_SPEED) != 0;
    const bool cache8MB = Environment::HasExtendedMainMemory();

    // The badge lives in VRAM bank C, so it must be mapped to the sub engine
    // background window (0x06200000) before any tile/tilemap data is written.
    // main.cpp maps it to LCDC for the border/capture path; take it over here
    // and hand it back in Clear().
    mem_setVramCMapping(MEM_VRAM_C_SUB_BG_00000);

    const char* text;
    if (!twlMode)
        text = "NTR MODE";
    else if (cache8MB)
        text = clock134MHz ? "TWL: 134MHz | 8MB CACHE" : "TWL: 67MHz | 8MB CACHE";
    else
        text = clock134MHz ? "TWL: 134MHz | 1MB CACHE" : "TWL: 67MHz | 1MB CACHE";

    const u32 length = (u32)strlen(text);

    // Build one 4bpp tile per character (16-bit VRAM stores only). Tile 0 is
    // left blank because it is the tile every cleared map cell references, so
    // the glyphs live at 1..length and the status row points at i + base.
    vu16* tiles = (vu16*)(STATUS_SUB_BG_BASE + STATUS_TILES_OFFSET);
    writeGlyphTile(tiles, nullptr); // tile 0 = blank background tile
    for (u32 i = 0; i < length; i++)
        writeGlyphTile(tiles + (i + STATUS_GLYPH_TILE_BASE) * STATUS_TILE_HALFWORDS,
            findGlyph(text[i]));

    // Wipe the whole 32x32 tilemap before writing the status row. Bank C can
    // still hold leftovers from the border/splash/previous run and any stale
    // entry would be drawn as a grid of garbage tiles behind and around the
    // badge. Every wiped cell now resolves to the blank tile 0.
    vu16* tilemap = (vu16*)(STATUS_SUB_BG_BASE + STATUS_TILEMAP_OFFSET);
    for (u32 i = 0; i < STATUS_TILEMAP_ENTRIES; i++)
        tilemap[i] = 0;

    // Centre the badge horizontally and place it just above where the bottom
    // border would be.
    const u32 startCol = (length < 32) ? (32 - length) / 2 : 0;
    for (u32 i = 0; i < length; i++)
        tilemap[STATUS_ROW * 32 + startCol + i] = (u16)(i + STATUS_GLYPH_TILE_BASE);

    GFX_PLTT_BG_SUB[0] = STATUS_BACKDROP_COLOR;
    GFX_PLTT_BG_SUB[1] = twlMode ? STATUS_TWL_COLOR : STATUS_NTR_COLOR;

    REG_BG0CNT_SUB = STATUS_BG0CNT_VALUE;
    // Force BG0 to be the only active sub layer. An |= here would leave any
    // sub BG/OBJ/window layer enabled by an earlier boot stage live, which
    // would render its own (uninitialised) VRAM over the badge. Bits 8..15 hold
    // all the sub layer/window enables, so clear them and set BG0 only.
    REG_DISPCNT_SUB = (REG_DISPCNT_SUB & ~0xFF00u) | (1 << 8); // MODE_0_2D | DISPLAY_BG0_ACTIVE
}

void StartupStatusIndicator::Clear()
{
    // Disable the layer first, then wipe the tilemap so a later BG3 setup can
    // never pick up stale entries. The wipe must happen while bank C is still
    // mapped to the sub BG window, so restore the LCDC mapping afterwards.
    REG_DISPCNT_SUB &= ~(1 << 8);
    REG_BG0CNT_SUB = 0;

    vu16* tilemap = (vu16*)(STATUS_SUB_BG_BASE + STATUS_TILEMAP_OFFSET);
    for (u32 i = 0; i < STATUS_TILEMAP_ENTRIES; i++)
        tilemap[i] = 0;

    // Hand VRAM bank C back to LCDC so GbaBorderService can load the border
    // through 0x06840000 and the capture double buffer can swap it to SUB_BG
    // per frame exactly as before the badge was added.
    mem_setVramCMapping(MEM_VRAM_C_LCDC);
}
