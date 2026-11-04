#include "common.h"
#include "PatchSwi.h"

extern void* patch_swiTable[32];

static int sNextFreePatchNumber = 0;

void patch_resetSwiPatches()
{
    sNextFreePatchNumber = 0;
}

int patch_addSwiPatch(void* function)
{
    if (sNextFreePatchNumber >= (int)(sizeof(patch_swiTable) / sizeof(patch_swiTable[0])))
    {
        gLogger->Log(LogLevel::Error, "Too many SWI patches\n");
        return -1;
    }
    int number = sNextFreePatchNumber++;
    patch_swiTable[number] = function;
    return number;
}
