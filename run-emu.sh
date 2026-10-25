#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Headless DS-run harness + screenshot.
#
# Boots a .nds ROM under DeSmuME inside Xvfb, lets it run for a while, then
# captures the screen to a PNG that can be inspected directly. This is how the
# GBARunner core can be exercised and observed without a physical console.
#
# --slot1-dir mounts a host directory as the slot-1 (DLDI) filesystem, which is
# how GBARunner reaches GBA ROMs and save files. Put `rom.gba` (and optionally
# `_gba/configs/*.json`) in that directory.
#
# Usage:
#   ./run-emu.sh <rom.nds> [--seconds N] [--slot1-dir DIR] [--out FILE]
#
# Env:
#   EMU     emulator binary      (default: desmume-cli)
#   SCREEN  Xvfb screen geometry (default: 512x384x24)
# ---------------------------------------------------------------------------
set -u

ROM=""
SECONDS_RUN=15
SLOT1=""
OUT="/tmp/opencode/emu-shot.png"
EMU="${EMU:-desmume-cli}"
SCREEN="${SCREEN:-512x384x24}"

while [ $# -gt 0 ]; do
    case "$1" in
        --seconds) SECONDS_RUN="$2"; shift 2 ;;
        --slot1-dir) SLOT1="$2"; shift 2 ;;
        --out) OUT="$2"; shift 2 ;;
        -h|--help) sed -n '2,22p' "$0"; exit 0 ;;
        *) ROM="$1"; shift ;;
    esac
done

if [ -z "$ROM" ] || [ ! -f "$ROM" ]; then
    echo "usage: $0 <rom.nds> [--seconds N] [--slot1-dir DIR] [--out FILE]" >&2
    exit 2
fi
if ! command -v "$EMU" >/dev/null 2>&1; then
    echo "❌ '$EMU' not found on PATH (install desmume, or set EMU=...)." >&2
    exit 1
fi
if ! command -v xvfb-run >/dev/null 2>&1; then
    echo "❌ xvfb-run not found (install xorg-server-xvfb)." >&2
    exit 1
fi

mkdir -p "$(dirname "$OUT")"
SLOT1_ARG=()
[ -n "$SLOT1" ] && SLOT1_ARG=(--slot1-fat-dir "$SLOT1")

# The emulator is launched inside a throwaway X server; `import` snapshots the
# whole root window (which includes the emulator window) after SECONDS_RUN.
xvfb-run -a -s "-screen 0 $SCREEN" bash -c "
    export SDL_AUDIODRIVER=dummy
    $EMU --disable-sound ${SLOT1_ARG[*]} '$(realpath "$ROM")' >/tmp/opencode/emu.log 2>&1 &
    EPID=\$!
    sleep $SECONDS_RUN
    import -window root '$OUT' 2>/dev/null
    kill \$EPID 2>/dev/null
    wait \$EPID 2>/dev/null
"
rc=$?
if [ -f "$OUT" ]; then
    echo "✅ screenshot: $OUT"
else
    echo "❌ no screenshot produced (rc=$rc); see /tmp/opencode/emu.log"
    exit 1
fi
