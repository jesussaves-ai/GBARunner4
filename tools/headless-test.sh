#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Headless test runner for the embedded-ROM GBARunner build.
#
# It replaces the old "write a screenshot to the SD card" loop with a channel
# that does not need any filesystem at all: the ARM9 test build publishes its
# boot stage / result through gTestReport in DS main RAM, and the libretro
# frontend (tools/lrshot.c) reads it straight out of RETRO_MEMORY_SYSTEM_RAM.
#
# Usage:
#   ./tools/headless-test.sh <rom.gba> [core.so]
#
# Env:
#   FRAMES   frames to emulate            (default 600)
#   CORE     libretro DS core             (default desmume)
#   OUTDIR   output directory             (default /tmp/opencode/headless)
#
# Exit status:
#   0  the VM started and no failure was reported
#   3  the test build never reached the GBA VM (see the printed stage)
#   4  the guest reported a failure through test_reportResult()
# ---------------------------------------------------------------------------
set -u
set -o pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ROM="${1:-}"
CORE="${2:-${CORE:-/usr/lib/libretro/desmume_libretro.so}}"
FRAMES="${FRAMES:-600}"
OUTDIR="${OUTDIR:-/tmp/opencode/headless}"

if [ -z "$ROM" ] || [ ! -f "$ROM" ]; then
    echo "usage: $0 <rom.gba> [core.so]" >&2
    exit 2
fi
if [ ! -f "$CORE" ]; then
    echo "❌ libretro core not found: $CORE" >&2
    exit 2
fi

mkdir -p "$OUTDIR"
NDS="$OUTDIR/test.nds"

echo "--- build ---"
if ! GBAR3_SCREENSHOT=0 "$ROOT/build-test.sh" "$ROM" "$NDS"; then
    exit 1
fi

# gTestReport lives in .ewram.bss (DS main RAM), so the frontend can read it
# from RETRO_MEMORY_SYSTEM_RAM. Resolve its address from the ELF each run.
NM="${NM:-$(command -v arm-none-eabi-nm || echo /opt/devkitpro/devkitARM/bin/arm-none-eabi-nm)}"
ADDR="$("$NM" "$ROOT/code/core/arm9/arm9.elf" 2>/dev/null | awk '$3 == "gTestReport" { print $1 }')"
if [ -z "$ADDR" ]; then
    echo "❌ gTestReport not found in arm9.elf" >&2
    exit 1
fi

LRSHOT="$OUTDIR/lrshot"
cc -O2 -I"$ROOT/tools" -o "$LRSHOT" "$ROOT/tools/lrshot.c" -ldl || exit 1

# melonDS needs direct boot + its ARM JIT disabled for GBARunner's system-mode
# switch; harmless for other cores.
OPTS=""
case "$(basename "$CORE")" in
    melonds*) OPTS="melonds_jit_enable=disabled;melonds_boot_directly=enabled" ;;
esac

echo "--- run ($FRAMES frames, $(basename "$CORE"))) ---"
SHOT="$OUTDIR/frame.ppm"
RAM="$OUTDIR/ram.bin"
LOG="$OUTDIR/lrshot.log"
LRSHOT_REPORT="0x$ADDR" LRSHOT_DUMP="$RAM" LRSHOT_SET_OPTIONS="$OPTS" \
    "$LRSHOT" "$CORE" "$NDS" "$FRAMES" "$SHOT" >"$LOG" 2>&1
rc=$?

REPORT="$(grep '^REPORT ' "$LOG" | tail -1)"
if [ -z "$REPORT" ]; then
    echo "❌ no report from the guest (frontend rc=$rc); see $LOG" >&2
    exit 1
fi
echo "$REPORT"

STAGE="$(echo "$REPORT" | sed -n 's/.* stage=\([0-9]*\).*/\1/p')"
RESULT="$(echo "$REPORT" | sed -n 's/.* result=\([0-9]*\).*/\1/p')"
FRAME="$(echo "$REPORT" | sed -n 's/.* frame=\([0-9]*\).*/\1/p')"

[ -s "$SHOT" ] && echo "frame: $SHOT"
[ -s "$RAM" ] && echo "ram:   $RAM"

if [ "$RESULT" = "2" ]; then
    echo "❌ guest reported FAILURE (stage=$STAGE)"
    exit 4
fi
if [ "${STAGE:-0}" -lt 100 ]; then
    echo "❌ test build stopped at stage $STAGE (the VM never started)"
    exit 3
fi
if [ "${FRAME:-0}" -eq 0 ]; then
    echo "⚠️  VM started but no VBlank IRQ was delivered (frame=0)."
    echo "   The capture is therefore not a valid GBA frame; this is an"
    echo "   emulator-side limitation, not a guest result."
    exit 3
fi

echo "✅ VM running (stage=$STAGE, frame=$FRAME)"
exit 0
