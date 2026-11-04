// Host-side test for the pure save-path builder.
// Build/run: g++ -std=c++17 -Icode/core/arm9/source \
//            code/test/host/save_path_test.cpp -o /tmp/save_path_test && /tmp/save_path_test
#include <cassert>
#include <cstring>
#include "SavePath.h"

static void expect(const char* romPath, const char* expected)
{
    char out[64];
    assert(sav_buildSavePath(romPath, out, sizeof(out)));
    assert(strcmp(out, expected) == 0);
}

int main()
{
    expect("/rom.gba", "/rom.sav");
    expect("/rom", "/rom.sav");
    expect("/rom.g", "/rom.sav");
    expect("/rom.", "/rom.sav");
    expect("/dir.gba/rom", "/dir.gba/rom.sav");
    expect("fat:/dir/rom.GBA", "fat:/dir/rom.sav");

    char small[8];
    assert(!sav_buildSavePath("/rom.gba", small, sizeof(small)));
    assert(!sav_buildSavePath("/rom.gba", nullptr, 64));
    assert(!sav_buildSavePath(nullptr, small, sizeof(small)));
    return 0;
}
