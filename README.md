# GBARunner4

**A native ARM9/ARM7 Game Boy Advance hypervisor and emulation core for stock Nintendo DS, DSi, and 3DS hardware.**

[![Nightly Build](https://github.com/jesussaves-ai/GBARunner4/actions/workflows/nightly.yml/badge.svg)](https://github.com/jesussaves-ai/GBARunner4/actions/workflows/nightly.yml)
[![Platform](https://img.shields.io/badge/platform-Nintendo%20DS%20%7C%20DSi%20%7C%203DS-9cf)](#overview)
[![Target](https://img.shields.io/badge/CPU-ARM9%20ARM946E--S%20%40%2067%2F133%20MHz-blue)](#architecture--memory-budgets)
[![License](https://img.shields.io/badge/license-MPL--2.0-informational)](#credits--license)

> **GBARunner4** is a fork of [Gericom's GBARunner3](https://github.com/Gericom/GBARunner3) by **[Jesus Saves (`jesussaves-ai`)](https://github.com/jesussaves-ai)** focused on **high-ROM compatibility, save robustness, and hardware-accurate peripheral emulation**. It is a **hardware-assisted DS homebrew solution** built for stock Nintendo DS, DSi, and 3DS hardware, launched from a **slot-1 flashcart** or a **DS-mode hardware bridge** such as the **DSpico (RP2040)**. Unlike standalone software emulators on other handhelds, it does not emulate the DS itself — it runs the original GBA code natively on the console's real ARM9/ARM7 processors.

---

## Table of Contents

- [Overview](#overview)
- [Key Custom Features & Fixes in this Fork](#key-custom-features--fixes-in-this-fork)
- [Architecture & Memory Budgets](#architecture--memory-budgets)
- [Build & Verification Instructions](#build--verification-instructions)
- [Save Type Reference](#save-type-reference)
- [Repository Layout](#repository-layout)
- [Credits & License](#credits--license)

---

## Overview

GBARunner4 is not a traditional software emulator, and it is not a standalone emulator for other handhelds. It is a **hardware-assisted DS homebrew hypervisor**: rather than emulating the DS, it runs the original cartridge code **natively** on the console's own ARM9 (ARM946E-S) and ARM7 (ARM7TDMI) processors, and only intercepts the instructions and memory accesses that the DS cannot execute directly. It is designed for stock Nintendo DS, DSi, and 3DS hardware, loaded through a slot-1 flashcart or a DS-mode hardware bridge such as the DSpico (RP2040).

This approach delivers near-native performance, but it requires an enormous amount of care around the hardest parts of the GBA platform:

- **High-ROM (hicode) execution** — GBA code that lives above the portion of the cartridge that is linearly mapped into DS memory. GBARunner4 pages it in through an **instruction-cache mapping** and **dynamic JIT patches**.
- **Save hardware emulation** — GBA cartridges ship with Flash, EEPROM, or SRAM save chips that games address through SDK routines buried anywhere in the ROM. These must be located and redirected to the SD card.
- **Peripheral fidelity** — DMA, timers, the affine background engine, direct-sound FIFOs, RTC/GPIO, and interrupts must behave closely enough that demanding titles do not glitch, desync, or click.

### Highlights

- 🎮 Runs GBA games **natively** on Nintendo DS / DSi / 3DS hardware.
- 💾 Native SD card access (DSi/3DS and flashcart SD), plus FatFS-based file I/O.
- 🧩 **Instruction-cache-mapped hicode** so games with code above the linear ROM window still boot and run.
- 💾 **Whole-ROM save-tag scanning** and permanent, eviction-safe ROM patching for Flash 512K/1M, EEPROM, and SRAM.
- 🔊 Hardware-accurate **Direct Sound FIFO** depth with overflow clamping for click-free streaming audio.
- 🖼️ Per-line **Mode 1 affine background** register latching for latency-sensitive racing titles.
- 🧱 **Strictly static memory layout** with a hard 32 KB ITCM budget for zero-waitstate critical routines.
- 📺 **Two-line boot diagnostics HUD** on the bottom screen (env line + `ID: <game code> | SAVE: <type>`), branded *GBARUNNER4 BY JESUS SAVES*, held ~1 s (A/Start to skip).
- 🔄 **`L + R + Start + Select` soft-reset** hotkey, debounced on the ARM7 over 30 vblanks.
- 🚀 **TWL auto-detection**: 134 MHz ARM9 clock and an 8 MB `.dsiram.bss` SdCache pool on DSi hardware.

### Target Hardware

| Platform | Notes |
| --- | --- |
| Nintendo DS / DS Lite | DS mode, ARM9 @ 67 MHz, ARM7 @ 33 MHz |
| Nintendo DSi / DSi XL | DSi mode, ARM9 @ 133 MHz, expanded RAM |
| Nintendo 3DS / 2DS | Backwards-compatible DS/DSi mode |
| Slot-1 flashcarts (R4, Acekard, DSTT, etc.) | FatFS-backed SD access; standard launch method |
| DSpico (RP2040) & DS-mode hardware bridges | Open-source slot-1 replacement / bridge hardware |

GBARunner4 is a **hardware-assisted DS homebrew solution**: it requires real DS-family hardware (or a DS-mode bridge) to run. It is distinct from standalone software emulators that emulate the DS in software on unrelated handhelds.

---

## Key Custom Features & Fixes in this Fork

### 1. High-ROM Safe Save Patching (Flash 1M/64K, EEPROM, SRAM)

Save SDK routines are frequently linked into the **paged high ROM** rather than the linearly mapped 2 MB window. Older patchers assumed the routines were linear and corrupted memory or left the real routine unpatched, causing games to poll the save chip forever.

- **Whole-ROM signature scanning** — `sdc_searchRomSignature()` and `sav_tryPatchFunction()` search both the linear window *and* the paged high ROM for patched-function signatures.
- **Permanent, eviction-safe patching** — `sdc_patchRom()` records every patch and **reapplies it whenever the containing 4 KB block is reloaded**, so a patch survives cache eviction.
- **4 KB block-boundary carry detection** — a 16-byte signature that starts in the last 12 bytes of a 4 KB block straddles into the next block. The scanner **carries the previous block's tail** and tests all three boundary offsets, so such routines are no longer missed.
- **Address-transparent entry patching** — `sav_patchRomFunction()` writes the Thumb `SWI` at the correct GBA address whether the routine is linear or paged.

### 2. SDCache Floor Guard

Permanently loading ROM blocks for patching consumes cache slots. A multi-patch load sequence could **underflow the replaceable-block counter or starve the cache**, manifesting as random crashes or missing code.

- A minimum floor of `SDC_MIN_EVICTABLE_BLOCKS` (16) replaceable blocks is permanently reserved.
- When the patch area is exhausted, loading **falls back to a normally replaceable block** instead of underflowing — the cache can neither starve nor `sBlockCount` wrap.

### 3. mGBA-Sourced `.rodata` Game-ID Override Table

Some titles ship **without a reliable SDK save tag**, or with an ambiguous one (e.g. 4 Kbit vs. 64 Kbit EEPROM). This fork adds a compact override table placed in `.rodata` so it never consumes the scarce ITCM.

- **Derived from the [mGBA](https://github.com/mgba-emu/mgba) cartridge override list**, the community reference for such titles.
- Corrects save **type and size** (e.g. forces 8 KB EEPROM for *Boktai*, *Super Mario Advance 2/3*, *Yoshi's Universal Gravitation*, and many more).
- Can force **no save chip** for titles that lock up or trip anti-piracy checks when a save chip is present (*Iridion II*, *Top Gun: Combat Zones*, *Stuart Little 2*).
- Handles the **Classic NES / Famicom Mini** series (game-code `F` suffix), which ships without a usable save tag.

### 4. Complete `0x0A`/`0x0C` ROM Mirror Prefetch & Register-Indirect BX Normalization

The GBA ROM is mirrored at `0x0A000000` and `0x0C000000`. A branch or load into a mirror address would translate to an unmapped DS address and **prefetch-abort forever**.

- **Prefetch abort path** normalizes mirror bits (`bic lr, lr, #0x06000000`) before computing the paged cache tag and resumed PC.
- **Register-indirect `BX` path** (`JitArmUndefinedBx.s`) normalizes the target the same way, so indirect jumps into a mirror resolve to the base `0x08` range.
- **PC-relative `LDR`** normalization (`JitArmUndefinedLdrPcImm.s`) folds its two bounds checks into a single range test.

### 5. Fixed SRAM Store Handler Tail-Branching

The SRAM store handlers in `MemoryStore8.s`, `MemoryStore16.s`, and `MemoryStore32.s` previously did:

```asm
    strb r9, [r10]
    bl   markSaveDirty   ; clobbers lr
    bx   lr              ; returns into the bl above -> infinite loop
```

`markSaveDirty` tails out with `bx lr`, so `bl` overwrote the original return address and corrupted the call stack during **boot-time SRAM mirroring**. This fork changes the sequence to a true tail branch:

```asm
    strb r9, [r10]
    b    markSaveDirty   ; lr still holds the original return address
```

### 6. Per-Line Mode 1 Affine Background HBlank Register Latching

*F-Zero: Maximum Velocity* and other Mode 1/7-style titles update the affine parameters **per scanline**. Because the DS reuses the GBA `BG2PA..PD` / `BG2X/Y` registers for its own bitmap transform, a per-line update could be sampled half-written, producing visible affine shearing.

- The HBlank IRQ handler **re-asserts the guest's complete BG2 affine matrix** (`PA/PB/PC/PD`) from the shadow registers to the 2D engine at every visible scanline boundary, then **drains the write buffer** with `mcr p15, 0, ..., c7, c10, 4`.
- `BG2X/BG2Y` are deliberately **not** rewritten — the engine latches the reference point at VBlank and accumulates it by `PB/PD` each line, so overwriting it would freeze the scroll.
- Affine registers are also made **read-back correct** (returns the shadowed value instead of open-bus garbage) and **re-applied on mode change** back to a tile/affine mode from a bitmap mode.

### 7. Direct-Sound FIFO: 8-Word Hardware Accuracy & Overflow Clamping

The real GBA Direct Sound FIFO is **8 words (32 bytes) deep**, and the sound DMA bursts 4 words whenever at least 4 words are free. A deeper software FIFO lets emulated DMA run ahead of the guest mixer, consuming not-yet-written samples and causing pops/clicks.

- `GBAS_DIRECT_FIFO_SIZE` is now **8 words**, and all offset math uses `GBAS_DIRECT_FIFO_MASK`.
- DMA is requested **exactly like the hardware half-empty point** (`!(fifoCount & 4)`), which also re-arms a refill after an underrun.
- A `SOUNDCNT_H` reset strobe sets `fifoReset`, so the ARM7 drops its cached playback word and the new stream starts on its own first sample instead of the previous stream's tail.
- A catch-up clamp (`GBAS_MAX_OVERFLOWS_PER_SAMPLE = 4`) prevents a timer reload with 10+ overflows from dumping a whole buffer in one step or holding a stale sample.

### Additional Fork Improvements

- **Cart GPIO / RTC bridge** (`RomGpio`, `RomGpioRtc`) with an ARM7 RTC IPC service for games that poll a real-time clock via `0x080000C4`. 32-bit (word) GPIO stores are split into their two 16-bit writes so word-store RTC/GPIO configuration is not dropped, and the registers stay trapped and coherent across the 2 MB linear ROM pre-map.
- **Whole-ROM patching for auxiliary patches** — `BadMixerPatch`, `HarvestMoonPatches`, and `SelfModifyingPatches` now use `sdc_searchRomSignature()` / `sdc_patchRom()` instead of assuming the first 2 MB.
- **TWL/DSi support** — runtime DSi detection, an ARM9 134 MHz clock boost, and an 8 MB `.dsiram.bss` SdCache pool (vs. the 1 MB NTR ring), with a boot status badge that reports the active mode.
- **2 MB linear ROM pre-mapping** — the first 2 MB of the cartridge is mapped directly at `0x02200000` and never goes through the SD cache; DMA and the memory emulator split cleanly at the `0x08200000` seam and normalize the `0x0A`/`0x0C` waitstate mirrors.
- **JIT / DMA SMC coherency** — `jit_invalidateCodeRange()` re-processes IWRAM/EWRAM that was overwritten by DMA (and clears the aux bits + invalidates the I-cache) only when the range had actually been JIT-processed, so data DMA pays only a short scan.
- **Timer IPC guards** — every timer `TMxCNT` write waits for room in the ARM9→ARM7 send FIFO before posting, so a dropped `SET_TIMER_RELOAD`/`SET_TIMER_CONTROL` can no longer desync the ARM7 direct-sound timer (pitch/crackle).
- **Save robustness** — full-device flash chip-erase, capture of the SDK read/verify entry points before the whole-ROM signature search (high-ROM Flash titles), a correct SRAM category test, `0xFF` on reads past EOF, and mGBA-interoperable EEPROM `.sav` byte order.
- **Super Mario Advance 4 (128 KB flash)** and the other mGBA-sourced game-id overrides.
- **Two-line boot diagnostics HUD** and the **`L + R + Start + Select`** ARM7 soft-reset hotkey (flush a dirty save, then a clean reset).
- **Numerous `.text`/ITCM size optimizations** (`-Os` hot paths, refactored FAT chains, disabled ArduinoJson float support, compile-time MPU tables, and more).

---

## Architecture & Memory Budgets

GBARunner4 splits responsibility across the two DS CPUs:

| CPU | Role |
| --- | --- |
| **ARM9** (ARM946E-S, 67/133 MHz) | Hypervisor, JIT patcher, virtual-machine exception handlers, memory emulation, graphics, save subsystem, SD cache |
| **ARM7** (ARM7TDMI, 33 MHz) | IPC services, GBA sound mixing, filesystem/SD access, RTC/GPIO bridge |

Runtime frameworks follow the ARMv5TE (ARM9) and ARMv4T (ARM7 / GBA mode) ISAs only — **no ARMv6/ARMv7+ instructions are ever emitted**.

### Memory Map (from `code/core/arm9/gbarunner9.ld`)

| Region | Origin | Length | Purpose |
| --- | --- | ---: | --- |
| `itcm` | `0x00000000` | **32,768 B (32 KB)** | Zero-waitstate critical routines |
| `dtcm` | `0xFFFFC000` | 16,384 B (16 KB) | VM dispatch, stacks, memory load/store tables |
| `ewram` | `0x02040000` | ~1.75 MB | Save buffers, SD cache, large static state |
| `vrama` | `0x06800000` | 128 KB | `.text`, `.rodata`, `.data`, `.bss` |
| `vramhi` | `0x06898000` | 48 KB | High-priority VRAM-resident BSS |

### ITCM: The Hard Limit

The ITCM is the single most scarce resource in the project and is capped at **exactly 32,768 bytes (32 KB)** — not a soft target, but the architectural limit of the ARM946E-S tightly-coupled memory. Only routines that must run with **zero waitstates** (exception vectors, VM dispatch, JIT undefined-instruction handlers, hot memory load/store paths, and the audio FIFO writers) are placed there via `.itcm` sections and the linker script's fixed address table.

The linker enforces the budget by region length; a build that overflows it will simply fail to link. `./verify.fish` reports actual usage.

### Allocation Policy: Zero Dynamic Allocation

The emulation core follows a strict **no `malloc`/`free`** policy (see `AGENTS.md`):

- The hypervisor, JIT patcher, memory emulator, peripherals, and save subsystem **never allocate at runtime**.
- Every buffer is **statically sized and placed into a fixed linker region** (`.ewram`, `.ewram.bss`, `.vramhi.bss`, `.dtcm`, `.itcm`).
- This guarantees deterministic timing, eliminates fragmentation, and prevents allocation failure during emulation.

> ℹ️ A single fixed TLSF arena exists only for the **pre-emulation** splash/settings path and is never touched while a game is running.

---

## Build & Verification Instructions

### Prerequisites

- **devkitARM** (devkitPro) with **libnds** — the toolchain provides `arm-none-eabi-gcc/g++`, `ndstool`, and `dlditool`.
- **GNU Make**.
- **Git** (with submodule support).
- Optional: **Docker** to use the pinned `devkitpro/devkitarm` image without a local toolchain.

```bash
# Using devkitPro pacman (recommended)
sudo dkp-pacman -S devkitARM libnds
export DEVKITARM=/opt/devkitpro/devkitARM
export DEVKITPRO=/opt/devkitpro
```

### Clone

The repo uses the **libtwl** submodule, so use `--recursive`:

```bash
git clone --recursive https://github.com/jesussaves-ai/GBARunner4.git
cd GBARunner4
```

If you cloned without submodules:

```bash
git submodule update --init --recursive
```

### Build

The top-level build compiles `libtwl`, the bootstrap loader, and the ARM7/ARM9 cores, then packages them into a `.nds` file:

```bash
make -C code
```

| Artifact | Path |
| --- | --- |
| ARM9 core ELF | `code/core/arm9/arm9.elf` |
| ARM7 core ELF | `code/core/arm7/GBARunner3.elf` |
| **Final ROM** | `code/bootstrap/GBARunner3.nds` |

The release workflow also copies the per-game `configs/` directory next to the ROM:

```
out/
├── GBARunner3.nds
└── _gba/
    └── <game-code>.json
```

### Verify the Memory Footprint

`verify.fish` runs a clean containerized build and prints the ARM9 section sizes, **including the ITCM usage** — the fastest way to confirm you have not blown the 32 KB budget:

```bash
./verify.fish
```

It performs the equivalent of:

```bash
docker run --rm -v (pwd):/gba devkitpro/devkitarm:20230928 bash -c "
    cd /gba/code && make -C bootstrap &&
    /opt/devkitpro/devkitARM/bin/arm-none-eabi-size -A /gba/code/core/arm9/*.elf \
        | grep -E 'Section|itcm|dtcm|text|bss|Total'
"
```

Example (`arm-none-eabi-size -A code/core/arm9/arm9.elf`):

| Section | Size (bytes) | Region |
| --- | ---: | --- |
| `.itcm` | **32,184 / 32,768** | ITCM (584 B headroom) |
| `.dtcm` | 16,384 / 16,384 | DTCM (fully laid out) |
| `.text` | 56,176 | VRAM A |
| `.rodata` | 3,900 | VRAM A |
| `.ewram` | 2,724 | EWRAM |
| `.ewram.bss` | 1,761,280 | EWRAM |
| `.vramhi.bss` | 49,152 | VRAM HI |
| `.bss` | 68,460 | VRAM A |

> The ITCM is intentionally kept **nearly full**. If a change overflows it, the link fails — move code out of `.itcm` or shrink it.

To find the largest ITCM/code symbols when you are close to the limit:

```bash
./itcm-list.fish
```

This prints the top symbols sorted by size using `arm-none-eabi-nm --print-size --size-sort`.

---

## Save Type Reference

| Save hardware | Sizes | Patch functions | Notes |
| --- | --- | --- | --- |
| SRAM | 32 KB | `sram_patchV110/V111` | Fast SRAM variants need no patch |
| EEPROM | 512 B, 8 KB | `eeprom_patchV111/V120/V124/V126` | 8 KB titles corrected via Game-ID override table |
| Flash 512K | 64 KB | `flash_patchV120/V123/V126`, `flash_patch512V130` | Macronix ID |
| Flash 1M | 128 KB | `flash_patch1MV102/V103` | Sector/paging aware |

Save tags (`FLASH`, `SRAM`, `EEPR`) are discovered by scanning the **entire ROM**; ambiguous or tag-less titles are resolved with the `.rodata` Game-ID override table described above.

---

## Repository Layout

```
code/
├── bootstrap/            # Loader that chains into the cores, produces GBARunner3.nds
├── core/
│   ├── arm7/             # ARM7 services: IPC, sound mixing, filesystem, RTC
│   ├── arm9/             # ARM9 hypervisor & emulation core
│   │   └── source/
│   │       ├── JitPatcher/       # Dynamic ROM patching & instruction decoding
│   │       ├── VirtualMachine/   # Hypervisor exception handlers
│   │       ├── MemoryEmulator/   # Load/store tables & hicode cache mapping
│   │       ├── Peripherals/      # DMA, sound, timers, graphics, GPIO/RTC
│   │       ├── Save/             # Flash/EEPROM/SRAM emulation & tag scanner
│   │       └── SdCache/          # Paged high-ROM block cache
│   └── common/           # Shared definitions
├── libs/libtwl/          # libtwl submodule (Gericom/libtwl)
└── test/                 # Standalone test builds
configs/                  # Per-game JSON configurations (JIT patch addresses)
docs/                     # Technical Reference Manual (LaTeX)
verify.fish               # Clean build + memory footprint check
itcm-list.fish            # Largest ITCM/code symbol report
```

Key folders are governed by [`AGENTS.md`](AGENTS.md).

---

## Credits & License

- **This fork (GBARunner4):** [Jesus Saves (`jesussaves-ai`)](https://github.com/jesussaves-ai) — high-ROM compatibility, save robustness, TWL/DSi support, peripheral and audio fixes, the boot diagnostics HUD, and the ARM7 soft-reset hotkey.
- **Original project:** [Gericom/GBARunner3](https://github.com/Gericom/GBARunner3) — the hypervisor architecture, hicode cache mapping, and `libtwl`.
- **Save override data:** derived from the [mGBA](https://github.com/mgba-emu/mgba) cartridge override tables (community save-type reference).
- **toolchain:** [devkitPro](https://devkitpro.org/) (`devkitARM` / `libnds`).

Source files and linker scripts carry **Mozilla Public License 2.0** headers; see those headers for the exact terms. This project is a community fork and is not affiliated with Nintendo or Gericom.
