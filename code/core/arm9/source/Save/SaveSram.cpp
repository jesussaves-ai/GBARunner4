#include "common.h"
#include "Save.h"
#include "SaveTypeInfo.h"
#include "SaveSram.h"

// All SRAM patch functions below are applied through sav_tryPatchFunction,
// which searches the whole rom (linear window + paged high rom) and writes the
// SWI via sav_patchRomFunction. SRAM functions above the 2 MB ROM_LINEAR_SIZE
// window are therefore patched in place instead of being mistaken for the
// linearly mapped copy.

static const u32 sReadSramV110Sig[] = { 0xB0A7B590u, 0x6038466Fu, 0x60BA6079u, 0x49174817u };
static const u32 sReadSramV111Sig[] = { 0xB0A0B570u, 0x1C0D1C04u, 0x4A081C16u, 0x49088810u };

static const u32 sWriteSramV110Sig[] = { 0xB083B580u, 0x6038466Fu, 0x60BA6079u, 0x49094809u };
static const u32 sWriteSramV111Sig[] = { 0x1C05B530u, 0x1C131C0Cu, 0x88104A0Bu, 0x4008490Bu };

static const u32 sVerifySramV110Sig[] = { 0xB0B7B590u, 0x6038466Fu, 0x60BA6079u, 0x49174817u };
static const u32 sVerifySramV111Sig[] = { 0xB0B0B570u, 0x1C0D1C04u, 0x4A081C16u, 0x49088810u };

static void copySram(const u8* src, u8* dst, u32 size)
{
    for (u32 i = 0; i < size; i++)
        *dst++ = *src++;
}

static const u8* verifySram(const u8* src, const u8* tgt, u32 size)
{
    for (u32 i = 0; i < size; ++i)
    {
        u8 saveByte = *tgt++;
        u8 expectedByte = *src++;
        if (saveByte != expectedByte)
            return tgt - 1;
    }
    return nullptr;
}

static bool patchSram(const u32* readSignature, const u32* writeSignature, const u32* verifySignature)
{
    SavePatch patches[] =
    {
        { readSignature, 0, (void*)copySram },
        { writeSignature, 1, (void*)copySram },
        { verifySignature, 2, (void*)verifySram },
    };
    return sav_tryPatchFunctions(patches, 3);
}

bool sram_patchV110(const SaveTypeInfo* saveTypeInfo, FIL* romFile, u32 tagRomAddress, u8* tempBuffer)
{
    return patchSram(sReadSramV110Sig, sWriteSramV110Sig, sVerifySramV110Sig);
}

bool sram_patchV111(const SaveTypeInfo* saveTypeInfo, FIL* romFile, u32 tagRomAddress, u8* tempBuffer)
{
    return patchSram(sReadSramV111Sig, sWriteSramV111Sig, sVerifySramV111Sig);
}
