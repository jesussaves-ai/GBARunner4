.section ".ewram.bss", "aw"

#include "SdCacheDefs.h"

.balign SDC_BLOCK_SIZE

@ NTR (DS/DS Lite) SdCache pool. The active pool is selected at runtime by
@ sdc_init(): this 1 MB buffer on NTR, or the DSi extended-memory pool on TWL.
.global sdc_cacheNtr
sdc_cacheNtr:
    .space SDC_NTR_SIZE

.end
