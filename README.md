# GBARunner4

**A high-performance ARM9/ARM7 Game Boy Advance hypervisor and emulation framework for the Nintendo DS, DSi (TWL), and 3DS.**

[![Nightly Build](https://github.com/jesussaves-ai/GBARunner4/actions/workflows/nightly.yml/badge.svg)](https://github.com/jesussaves-ai/GBARunner4/actions/workflows/nightly.yml)
[![Release](https://github.com/jesussaves-ai/GBARunner4/actions/workflows/release.yml/badge.svg)](https://github.com/jesussaves-ai/GBARunner4/actions/workflows/release.yml)
[![Platform](https://img.shields.io/badge/platform-Nintendo%20DS%20%7C%20DSi%20%7C%203DS-9cf)](#target-hardware)
[![CPU](https://img.shields.io/badge/CPU-ARM9%20ARM946E--S%20%40%2067%2F134%20MHz-blue)](#architecture--memory-budgets)
[![License](https://img.shields.io/badge/license-MPL--2.0-informational)](#credits--license)

> **GBARunner4** is a fork of [Gericom's GBARunner3](https://github.com/Gericom/GBARunner3), maintained by **[Jesus Saves (`jesussaves-ai`)](https://github.com/jesussaves-ai)**. It is a **hardware-assisted GBA hypervisor**: instead of emulating the Nintendo DS in software, it runs the original cartridge code **natively** on the console's own ARM9 (ARM946E-S) and ARM7 (ARM7TDMI) processors, trapping only the instructions and memory accesses the DS cannot execute directly. This fork focuses on **high-ROM compatibility, save robustness, DSi/TWL acceleration, and hardware-accurate peripheral emulation**, and adds a suite of user-facing quality-of-life features.

---

## Table of Contents

- [Overview](#overview)
- [Target Hardware](#target-hardware)
- [Key Technical Highlights & Architectural Upgrades](#key-technical-highlights--architectural-upgrades)
  - [DSi / TWL Integration](#1-dsi--twl-integration)
  - [Zero-Waitstate ITCM Optimization](#2-zero-waitstate-itcm-optimization)
  - [Linear ROM Pre-Mapping](#3-linear-rom-pre-mapping)
  - [Conditional JIT SMC Coherency](#4-conditional-jit-smc-coherency)
  - [Audio Engine Overhaul](#5-audio-engine-overhaul)
  - [Save & Hardware Compatibility](#6-save--hardware-compatibility)
- [User-Facing Features & Quality of Life](#user-facing-features--quality-of-life)
- [Architecture & Memory Budgets](#architecture--memory-budgets)
- [Build & Verification](#build--verification)
- [GitHub Release Packaging & Automation](#github-release-packaging--automation)
- [Save Type Reference](#save-type-reference)
- [Repository Layout](#repository-layout)
- [Credits & License](#credits--license)

---

## Overview

GBARunner4 is **not** a standalone software emulator for unrelated handhelds, and it does **not** emulate the DS. It is a **hardware-assisted DS homebrew hypervisor**: it requires real DS-family hardware (or a DS-mode bridge) and runs the original GBA cartridge code natively on the console's real ARM9/ARM7 CPUs, intercepting only what the hardware cannot do natively.

That approach delivers near-native performance, but it demands extreme care around the hardest corners of the GBA platform:

- **High-ROM (hicode) execution** — GBA code that lives above the portion of the cartridge linearly mapped into DS memory. GBARunner4 pages it in through an **instruction-cache mapping** and **dynamic JIT patches**.
- **Save-hardware emulation** — GBA cartridges ship with Flash, EEPROM, or SRAM chips that games address through SDK routines buried anywhere in the ROM. These routines must be found and redirected to the SD card.
- **Peripheral fidelity** — DMA, timers, the affine background engine, Direct Sound FIFOs, RTC/GPIO and interrupts must behave closely enough that demanding titles never glitch, desync, or click.

### Highlights

- 🎮 Runs GBA games **natively** on Nintendo DS / DS Lite / DSi / 3DS and DS-mode bridges.
- ⚡ **TWL auto-detection** — 134 MHz ARM9 clock boost and an **8 MB `.dsiram.bss`** SD cache pool on DSi hardware (960 KB ring on DS/DS Lite).
- 🧱 **Zero-waitstate ITCM discipline** — a hard **32,768-byte** budget with safe headroom (`memu_thumbGetR0_0` pinned at `0x3000`).
- 🗺️ **Linear ROM pre-mapping** — the first 2 MB of the cartridge is mapped directly to DS main memory for zero-SD-load execution.
- 🧩 **Instruction-cache-mapped hicode** so games with code above the linear window still boot and run.
- 💾 **Whole-ROM save-tag scanning** and eviction-safe, persistent ROM patching for Flash 512K/1M, EEPROM, and SRAM.
- 🔊 **Direct Sound FIFO-reset and de-click overhaul** — click-free map transitions and track changes.
- 🕹️ **Full S-3511A RTC GPIO handling** for clock-aware titles (Pokémon Ruby/Sapphire/Emerald).
- 📺 **Multi-line boot diagnostics HUD** (branding + environment + `ID: <code> | SAVE: <type>`).
- 🔄 **`L + R + Start + Select`** ARM7 soft-reset, plus **`L + R + Y`** DS Lite backlight cycling.

---

## Target Hardware

| Platform | Notes |
| --- | --- |
| Nintendo DS / DS Lite (NTR) | DS mode, ARM9 @ 67 MHz, ARM7 @ 33 MHz, 4 MB main RAM |
| Nintendo DSi / DSi XL (TWL) | DSi mode, ARM9 @ 134 MHz, 16 MB main RAM |
| Nintendo 3DS / 2DS | Backwards-compatible DS/DSi mode |
| Slot-1 flashcarts (R4, Acekard, DSTT, …) | FatFS / DLDI-backed SD access |
| DSpico (RP2040) & DS-mode hardware bridges | Open-source slot-1 replacement / bridge |

GBARunner4 is launched through a **slot-1 flashcart**, a **DS-mode hardware bridge** such as the **DSpico (RP2040)**, or **TWiLight Menu++**.

---

## Key Technical Highlights & Architectural Upgrades

### 1. DSi / TWL Integration

This fork is DSi-aware at every layer, from boot detection to the SD cache pool.

- **Runtime DSi detection** (`Environment::IsDsiMode()`) inspects the SCFG status register (`0x04004000`), so a single binary serves NTR and TWL hardware.
- **134 MHz ARM9 clock boost** — `scfg_setArm9Clock()` (`code/core/arm9/source/arm9Clock.s`) flips `SCFG_CLK` bit 0 (`0x04004004`), switching the ARM9 from 67 MHz to 134 MHz. The sequence masks IRQ/FIQ inside a critical section, strobes the clock, and dwells to let the PLL stabilize.
- **Dynamic 8 MB `.dsiram.bss` cache pool** — when the hardware exposes the full 16 MB of DSi main RAM (SCFG_EXT main-RAM limit ≥ 2, checked by `Environment::HasExtendedMainMemory()`), `sdc_init()` selects the 8 MB TWL cache pool (2,048 × 4 KB blocks) placed in the `0x02400000–0x02FFFFFF` extended window. On NTR it falls back to the 960 KB ring in `.ewram.bss`.
- **Boot badge reflects the active mode** — the HUD prints `TWL: 134MHZ | 8MB CACHE` (or the matching NTR/1 MB variant).

### 2. Zero-Waitstate ITCM Optimization

The ARM946E-S tightly-coupled instruction memory (`0x00000000`, **exactly 32,768 bytes**) is the single most scarce resource in the project. Only routines that must run with zero waitstates live there: exception vectors, VM dispatch, JIT undefined-instruction handlers, the hot memory load/store paths, and the audio FIFO writers.

- The linker **enforces the budget by region length**, so an overflow fails the link rather than silently corrupting memory.
- **Fixed address constraints are preserved** — the dispatcher tables in the `0x2000–0x3000` block are pinned to fixed addresses so dispatchers can compute jumps via bit shifts without pointer loads. `memu_thumbGetR0_0` remains at `0x3000`; any bytes added to that block are reclaimed inside it.
- The current build keeps a **safe ~1,072–1,344 byte headroom** (`32,768 − .itcm`) while remaining "nearly full" by design.
- Cold logic (save tag scanner, patchers, GPIO/RTC state machines, JSON, FAT chains) is deliberately kept out of `.itcm` and lives in `.text`/`.rodata`, with large read-only tables pushed into `.ewram`/`.ewram.bss`.

### 3. Linear ROM Pre-Mapping

The first **2 MB** of the cartridge (`[0x08000000, 0x08200000)`) is pre-mapped **directly** to DS main RAM at `ROM_LINEAR_DS_ADDRESS` (`0x02200000`, offset `-0x05E00000`).

- Reads and code fetches inside this window **never touch the SD cache**, eliminating SD latency for the majority of GBA titles.
- The DMA fast path and the memory emulator split cleanly at the `0x08200000` seam, and both normalize the `0x0A`/`0x0C` ROM waitstate mirrors (`bic reg, reg, #0x06000000`) before address translation and cache-tag lookup.
- Blocks in the linear window are represented as permanently resident in the SD cache mapping, so a `sdc_getRomBlock()` for the linear region returns the mapped pointer immediately.

### 4. Conditional JIT SMC Coherency

When a guest program DMAs code into IWRAM/EWRAM, the JIT may be holding stale translated blocks. This fork adds **intelligent, low-overhead range invalidation**:

- `jit_invalidateCodeRange()` (`code/core/arm9/source/JitPatcher/JitCommon.c`) is invoked from the DMA transfer path (`DmaTransfer.c`) for IWRAM/EWRAM destinations.
- It **only re-processes the range when that range had actually been JIT-processed** — the dirty/processed bits are consulted first, so a plain data DMA pays only a short scan instead of a full re-decode.
- When it does fire, it clears the auxiliary JIT bits and invalidates the ARM9 instruction cache for the affected range, restoring self-modifying-code coherency without penalizing ordinary data transfers.

### 5. Audio Engine Overhaul

Direct Sound on real hardware is **8 words (32 bytes) deep**; a deeper software FIFO lets DMA run ahead of the guest mixer and produces periodic pops/clicks. This fork aligns the engine with the hardware and eliminates stream-edge artifacts.

- **FIFO-reset starvation fixed** — a `SOUNDCNT_H` reset strobe no longer discards the pending `dmaRequest`, so the shallow 8-word FIFO is re-armed immediately after a reset instead of draining silently.
- **Level-triggered refill** — the refill request is armed exactly at the hardware half-empty point (`!(fifoCount & 4)`) on every DS output sample, including the zero-overflow path, so a low-rate Direct-Sound timer or a tempo/timer reprogram during a map transition cannot let the FIFO run dry.
- **ARM7 Timer-1 cascade support** — Timer 1 can cascade from Timer 0's overflow exactly like the GBA, so dual-timer audio rates stay in lock-step.
- **Bounded DAC de-click shaping** — a genuinely stopped stream releases its stuck DC level, and a reset/recovery is crossfaded (slew-limited) rather than stepped. A catch-up clamp (`GBAS_MAX_OVERFLOWS_PER_SAMPLE = 4`) prevents a timer reload with 10+ pending overflows from dumping a whole buffer in one step.
- **Result:** map-transition reloads and track changes no longer emit square-wave crackle (validated on *Super Mario Advance 4*).

### 6. Save & Hardware Compatibility

Save SDK routines frequently live in the **paged high ROM**, not the linear window, and cartridges use several different save chips. This fork hardens the whole pipeline.

- **Whole-ROM signature scanning** — `sdc_searchRomSignature()` searches both the linear window and the paged high ROM, handling signatures that **straddle a 4 KB boundary** via boundary carry bytes.
- **Persistent, eviction-safe patching** — `sdc_patchRom()` records every patch and reapplies it whenever the containing 4 KB block is reloaded, so a patch survives cache eviction.
- **SD cache floor guard** — a permanent floor of `SDC_MIN_EVICTABLE_BLOCKS` (16) replaceable blocks prevents multi-patch sequences from underflowing the cache.
- **Multi-vendor Flash support** — a `FLASH1M` override for *Super Mario Advance 4* (`AX4*`, 128 KB flash), plus a compact mGBA-sourced Game-ID override table (in `.ewram`, to keep the tight vrama region free) for tag-less and ambiguous titles (Classic NES `F…` codes, "liar carts" such as *Iridion II* and *Top Gun*, 8 KB EEPROM corrections, and more).
- **S-3511A RTC GPIO** — writes to `0x080000C4/0xC6/0xC8` bridge to `rio_write` (including word stores split into their two 16-bit halves), served by the ARM7 `RtcIpcService`, so RTC-polling titles never hang on the serial ready bit.

---

## User-Facing Features & Quality of Life

### Multi-Line Startup Diagnostic HUD

While the ROM loads, a bottom-screen badge (a 4bpp text BG0 on the sub engine) shows:

- a **branding row** — `GBARUNNER4 BY JESUS SAVES`,
- an **environment row** — `TWL: 134MHZ | 8MB CACHE` (or e.g. `TWL: 67MHZ | 1MB CACHE` / `NTR MODE`),
- a **game row** — `ID: <XXXX> | SAVE: <TYPE>` (shown as `SAVE: SCANNING...` while the whole-ROM save scan runs, then updated in place).

The badge is held for a smooth **~60-frame (~1 s)** VBound hold, and can be skipped early with **A** or **Start**. It is cleanly unmapped before the GBA display and border take over the sub screen.

### ARM7 Soft-Reset Hotkey

Holding **`L + R + Start + Select`** for **30 consecutive vblanks** triggers a warm reboot: any dirty save is flushed to the SD card, then the loader restarts cleanly. Detection runs on the ARM7 (per-vblank), so it costs no ARM9/ITCM budget, and the 30-frame hold requirement prevents a stray simultaneous press from resetting the game.

### DS Lite Backlight Brightness Cycling

On physical **DS Lite / DSi** consoles, **`L + R + Y`** cycles the backlight level by writing **PMIC register 4**. The handler verifies the DS-Lite/DSi read-only high-nibble signature (`0x40`) first, so original DS Phat and unknown hardware are left untouched (avoiding clobbering the audio-amp enable bits).

---

## Architecture & Memory Budgets

GBARunner4 splits responsibility across the two DS CPUs:

| CPU | Core | Clock | Role |
| --- | --- | --- | --- |
| **ARM9** | ARM946E-S | 67 MHz (NTR) / 134 MHz (TWL) | GBA CPU hypervisor, JIT patcher, VM exception handlers, memory emulation, 2D video, save subsystem, SD cache |
| **ARM7** | ARM7TDMI | 33 MHz | Direct Sound mixing, PSG/GBA channels, RTC polling, timers, X/Y/Touch/Hinge input IPC |

Runtime code follows the **ARMv5TE (ARM9)** and **ARMv4T (ARM7)** ISAs only — **no ARMv6/ARMv7/Thumb-2 instructions are ever emitted**, and there is **no hardware FPU** (all math is fixed-point).

### Memory Map

Regions are defined in `code/core/arm9/gbarunner9.ld`:

| Region | Origin | Length | Purpose |
| --- | --- | ---: | --- |
| `.itcm` | `0x00000000` | **32,768 B (hard limit)** | Zero-waitstate critical routines |
| `.dtcm` | `0xFFFFC000` | 16,384 B | VM dispatch, stacks, shadowed GBA I/O, load/store tables |
| `.ewram.bss` | `0x02040000` | ~1.7 MB | Save buffers, NTR SD cache ring, patch lists, cluster table |
| `.vrama` (`.text`/`.rodata`/`.bss`) | `0x06800000` | 128 KB | ARM9 code, read-only data, default BSS |
| `.vramhi.bss` | `0x06898000` | 48 KB | High-priority VRAM-resident BSS |
| `.dsiram.bss` | DSi extended RAM | 8 MB | TWL SD cache pool (only referenced when 16 MB is present) |

### Current Build Footprint

Measured with `arm-none-eabi-size -A code/core/arm9/arm9.elf`:

| Section | Size (bytes) | Region / Notes |
| --- | ---: | --- |
| `.text` | **59,736** | VRAM A — ARM9 code |
| `.itcm` | **31,696 / 32,768** | ITCM — **1,072 B headroom** |
| `.dtcm` | **16,384 / 16,384** | DTCM — fully laid out |
| `.rodata` | 3,668 | VRAM A |
| `.data` | 36 | VRAM A |
| `.ewram` | 3,608 | EWRAM |
| `.ewram.bss` | **1,691,648** | EWRAM |
| `.vramhi.bss` | 49,152 | VRAM HI |
| `.dsiram.bss` | **8,388,608** | DSi extended RAM (TWL cache pool) |
| `.bss` | 816 | VRAM A |

> The ITCM is intentionally kept **nearly full**. If a change overflows it, the link fails — move code out of `.itcm` or shrink it.

### Allocation Policy: Zero Dynamic Allocation

The emulation core follows a strict **no `malloc`/`free`** policy:

- The hypervisor, JIT patcher, memory emulator, peripherals, and save subsystem **never allocate at runtime**.
- Every buffer is **statically sized and placed into a fixed linker region**.
- This guarantees deterministic timing, eliminates fragmentation, and prevents allocation failure during emulation.

> ℹ️ A single fixed TLSF arena exists only for the **pre-emulation** splash/settings path and is never touched while a game is running. The optional 64 KB color-correction LUT is allocated from this arena once during display setup, and **only when color correction is enabled** (it is off by default), so it costs no static RAM in the common case.

---

## Build & Verification

### Prerequisites

- **devkitARM** (devkitPro) with **libnds** — provides `arm-none-eabi-gcc/g++`, `ndstool`, and `dlditool`.
- **GNU Make**, **Git** (with submodule support).
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
# or, if already cloned without submodules:
git submodule update --init --recursive
```

### Build

```bash
make -C code
```

| Artifact | Path |
| --- | --- |
| ARM9 core ELF | `code/core/arm9/arm9.elf` |
| ARM7 core ELF | `code/core/arm7/GBARunner3.elf` |
| **Final ROM** | `code/bootstrap/GBARunner3.nds` |

> The on-disk filename remains `GBARunner3.nds` **on purpose**, so flashcarts, the **DSpico**, and **TWiLight Menu++** keep finding it at their expected path. The NDS banner and title metadata identify it as **GBARunner4 by Jesus Saves**.

### Verify the Memory Footprint

```bash
./verify.fish
```

`verify.fish` runs a containerized build, prints the **ARM9 and ARM7** section sizes, and then **enforces the hardware-bounded regions**: `.itcm ≤ 32,768`, `.dtcm ≤ 16,384`, and `.vramhi.bss ≤ 49,152`. It fails with a non-zero exit code if any of them overflows, so the tight ITCM margin and the fully-used DTCM/VRAM-HI pools can never regress silently. Use `./itcm-list.fish` to list the largest ITCM/code symbols when you are close to the limit.

### Automated Tests

`code/test/` builds a **gtest-on-ARM9** `test.nds` that runs under a DS emulator supporting no$gba's nocash debug output (`0x04FFFA00`):

```bash
docker run --rm -v "$PWD":/gba devkitpro/devkitarm:20230928 bash -c "cd /gba/code/test && make"
```

The suite covers the ARM/Thumb memory emulator, the virtual-machine handlers, and the **JIT patcher** (Thumb `b cond` patch/decode round-trip and ARM control-flow patching). It prints a pass/fail summary (and the name of every failing test) to the **top screen**, so it runs directly on real hardware such as a **DSpico** with no emulator debug output; it also publishes the aggregate result to the `gTestResults` symbol (`[0]='TEST'`, `[1]=failed`, `[2]=total`, `[3]='DONE'`) for scripted/headless runs.

---

## GitHub Release Packaging & Automation

The compiled `GBARunner3.nds` is published straight to **GitHub Releases**, keeping the flashcart / DSpico / TWiLight Menu++ layout unchanged while clearly branding the binary as GBARunner4.

### Release Layout

```
GBARunner4.zip                 # GitHub Release asset (raw GBARunner3.nds uploaded alongside)
├── GBARunner3.nds             # NDS banner/title: "GBARunner4; By Jesus Saves"
└── _gba/
    └── configs/
        └── <GAMEID><XX>.json  # per-game JIT patch addresses
```

The `_gba/configs/<GAMEID>.json` path matches what the loader reads (`/_gba/configs/%c%c%c%c%02X.json`).

### Automated Workflow

Publishing a GitHub Release with a tag automatically triggers `.github/workflows/release.yml`, which builds with the pinned `devkitpro/devkitarm` container, packages the ROM and configs, and uploads them to the release.

### Manual Publish with the GitHub CLI

Build and package first, then publish in one command:

```bash
make -C code
mkdir -p out/_gba && cp -f code/bootstrap/GBARunner3.nds out/ && cp -r configs out/_gba/
cd out && zip -r ../GBARunner4.zip * && cd ..

gh release create v4.0.0 \
  out/GBARunner3.nds GBARunner4.zip \
  --title "GBARunner4 v4.0.0" \
  --notes "GBARunner4 v4.0.0 — ARM9/ARM7 GBA hypervisor for Nintendo DS / DSi / 3DS.
Launch GBARunner3.nds from a slot-1 flashcart, DSpico, or TWiLight Menu++."
```

### Helper Script

A convenience wrapper, `./release.fish`, performs the build, packaging, and optional publishing:

```bash
./release.fish            # build + package only (no publish)
./release.fish v4.0.0     # build + package + create/upload a GitHub release
```

---

## Save Type Reference

| Save hardware | Sizes | Patch functions | Notes |
| --- | --- | --- | --- |
| SRAM | 32 KB | `sram_patchV110/V111` | Newly initialised to `0xFF`; fast SRAM variants need no patch |
| EEPROM | 512 B, 8 KB | `eeprom_patchV111/V120/V124/V126` | 8 KB titles corrected via the Game-ID override table |
| Flash 512K | 64 KB | `flash_patchV120/V123/V126`, `flash_patch512V130` | Macronix ID |
| Flash 1M | 128 KB | `flash_patch1MV102/V103` | Sector/paging aware; `FLASH1M` override for *Super Mario Advance 4* |

Save tags (`FLASH`, `SRAM`, `EEPR`) are discovered by scanning the **entire ROM**; ambiguous or tag-less titles are resolved with the `.ewram` Game-ID override table.

---

## Repository Layout

```
code/
├── bootstrap/            # Loader that chains into the cores; produces GBARunner3.nds
├── core/
│   ├── arm7/             # ARM7 services: IPC, sound mixing, filesystem, RTC, timers
│   ├── arm9/             # ARM9 hypervisor & emulation core
│   │   └── source/
│   │       ├── JitPatcher/       # Dynamic ROM patching & instruction decoding
│   │       ├── VirtualMachine/   # Hypervisor exception handlers
│   │       ├── MemoryEmulator/   # Load/store tables & hicode cache mapping
│   │       ├── Peripherals/      # DMA, sound, timers, graphics, GPIO/RTC
│   │       ├── Save/             # Flash/EEPROM/SRAM emulation & tag scanner
│   │       └── SdCache/          # Paged high-ROM block cache (NTR + TWL pools)
│   └── common/           # Shared definitions
├── libs/libtwl/          # libtwl submodule (Gericom/libtwl)
└── test/                 # Standalone test builds
configs/                  # Per-game JSON configurations (JIT patch addresses)
docs/                     # Technical Reference Manual (LaTeX)
verify.fish               # Clean build + memory footprint check
itcm-list.fish            # Largest ITCM/code symbol report
release.fish              # Build + package + publish helper
```

Key folders are governed by [`AGENTS.md`](AGENTS.md).

---

## Credits & License

- **This fork (GBARunner4):** [Jesus Saves (`jesussaves-ai`)](https://github.com/jesussaves-ai) — high-ROM compatibility, save robustness, DSi/TWL acceleration, linear ROM pre-mapping, JIT SMC coherency, audio and peripheral fixes, the boot diagnostics HUD, and the ARM7 reset/backlight hotkeys.
- **Original project:** [Gericom/GBARunner3](https://github.com/Gericom/GBARunner3) — the hypervisor architecture, hicode cache mapping, and `libtwl`.
- **Save override data:** derived from the [mGBA](https://github.com/mgba-emu/mgba) cartridge override tables (community save-type reference).
- **Toolchain:** [devkitPro](https://devkitpro.org/) (`devkitARM` / `libnds`).

Source files and linker scripts carry **Mozilla Public License 2.0** headers; see those headers for the exact terms. This project is a community fork and is not affiliated with Nintendo or Gericom.
