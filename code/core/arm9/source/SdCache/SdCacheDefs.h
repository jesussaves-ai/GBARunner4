#pragma once

#define SDC_BLOCK_SHIFT         12
#define SDC_BLOCK_SIZE          (1 << SDC_BLOCK_SHIFT)
#define SDC_BLOCK_MASK          (SDC_BLOCK_SIZE - 1)
#define SDC_BLOCK_INVALID       0xFFFF
#define SDC_ROM_BLOCK_INVALID   0xFFFF

// NTR (DS/DS Lite): a ring that fits the tight 4 MB main RAM budget. It starts
// at 960 KB rather than 1 MB: the 64 KB difference lets the 64 KB gColorLut
// live in .ewram.bss (reclaiming half of the tight 128 KB vrama region) without
// shrinking the heap, while keeping 240 evictable 4 KB blocks for high-ROM
// streaming. TWL hardware still uses the full 8 MB DSi pool below.
#define SDC_NTR_SIZE            (960 * 1024)
#define SDC_NTR_BLOCK_COUNT     (SDC_NTR_SIZE / SDC_BLOCK_SIZE)

// TWL (DSi/3DS/DSpico in TWL mode): the 16 MB main RAM pool lets the same
// 4 KB ring grow eight-fold, slashing SD card read latency for multi-megabyte
// GBA titles. The pool lives in the DSi extended RAM and is only used when the
// hardware actually exposes the full 16 MB (see sdc_init()).
#define SDC_TWL_SIZE            (8 * 1024 * 1024)
#define SDC_TWL_BLOCK_COUNT     (SDC_TWL_SIZE / SDC_BLOCK_SIZE)

// Compile-time upper bound. Every statically sized structure that is indexed by
// a cache block number must be sized with this, while the runtime sBlockCount
// selects how many blocks the active pool actually owns.
#define SDC_MAX_BLOCK_COUNT     SDC_TWL_BLOCK_COUNT
#define SDC_MAX_SIZE            SDC_TWL_SIZE

// Legacy aliases: the original single-pool configuration (NTR). Kept so that
// the JIT bit tables and other consumers keep their historic 1 MB coverage.
#define SDC_SIZE                SDC_NTR_SIZE
#define SDC_BLOCK_COUNT         SDC_NTR_BLOCK_COUNT

#define SDC_ROM_BLOCK_COUNT     (32 * 1024 * 1024 / SDC_BLOCK_SIZE)
