#include "common.h"
#include "SdCache/SdCacheDefs.h"

// The JIT indexes its processed/aux bit tables through the active SdCache pool
// pointer. The JIT unit tests point this at a scratch buffer (see
// JitPatcher/JitThumbTests.cpp) instead of a real SD card cache, so SdCache.c
// itself is not linked into the test harness.
u8 (*sdc_cache)[SDC_BLOCK_SIZE] = nullptr;
