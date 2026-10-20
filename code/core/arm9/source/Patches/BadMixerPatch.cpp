#include "common.h"
#include "SdCache/SdCache.h"
#include "BadMixerPatch.h"

static const u32 sBuggedMixerSignature[4] = { 0xE92D1FF0u, 0xE8B0000Fu, 0xE1A03203u, 0xE0822001u };

bool BadMixerPatch::TryApplyPatch()
{
    // Search the whole rom (including the paged high rom) instead of assuming the
    // function lives in the first 2 MB. sdc_patchRom keeps the patch applied even
    // if the containing high-rom block is evicted and reloaded.
    u32 function = sdc_searchRomSignature(sBuggedMixerSignature);
    if (!function)
    {
        return false;
    }

    // remove the writeback
    sdc_patchRom(function + 4, 0xE890000F, 4);

    // some games contain it twice for some reason
    function = sdc_searchRomSignature(sBuggedMixerSignature);
    if (function)
    {
        sdc_patchRom(function + 4, 0xE890000F, 4);
    }

    return true;
}
