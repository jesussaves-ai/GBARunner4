#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Headless DS run + screenshot via RetroArch (null video/audio drivers).
#
# Runs a .nds for N frames and writes a screenshot at the end. No X server, no
# GL, no window, so it cannot disturb a desktop session. Uses the libretro
# desmume core by default; GBARunner's splash/HUD are visible this way.
#
# Usage:
#   ./run-ds.sh <rom.nds> [frames] [out.png] [core.so]
#
# Env:
#   SYS_DIR   system directory handed to the core (BIOS/firmware), optional
# ---------------------------------------------------------------------------
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="${TMPDIR:-/tmp}/lrds-build"
CORE="${CORE:-/usr/lib/libretro/desmume_libretro.so}"

ROM="${1:-}"
FRAMES="${2:-400}"
OUT="${3:-$BUILD/shot.png}"
[ $# -ge 4 ] && CORE="$4"

if [ -z "$ROM" ] || [ ! -f "$ROM" ]; then
    echo "usage: $0 <rom.nds> [frames] [out.png] [core.so]" >&2
    exit 2
fi
if ! command -v retroarch >/dev/null 2>&1; then
    echo "❌ retroarch not found (pacman -S libretro-desmume pulls it)" >&2
    exit 1
fi
[ -f "$CORE" ] || { echo "❌ core not found: $CORE"; exit 1; }

mkdir -p "$BUILD/home" "$BUILD/shots"
CONF="$BUILD/retroarch.cfg"
{
    echo 'video_driver = "null"'
    echo 'audio_driver = "null"'
    echo 'input_driver = "null"'
    echo 'input_joypad_driver = "null"'
    echo 'menu_driver = "null"'
    echo "libretro_directory = \"$(dirname "$CORE")\""
    echo "screenshot_directory = \"$BUILD/shots\""
    echo "savefile_directory = \"$BUILD/saves\""
    echo "savestate_directory = \"$BUILD/states\""
    [ -n "${SYS_DIR:-}" ] && echo "system_directory = \"$SYS_DIR\""
    echo 'log_verbosity = false'
} > "$CONF"

XDG_CONFIG_HOME="$BUILD/home" retroarch -c "$CONF" -L "$CORE" "$ROM" \
    --max-frames="$FRAMES" --max-frames-ss --max-frames-ss-path="$OUT" \
    >"$BUILD/ra.log" 2>&1
rc=$?

if [ -s "$OUT" ]; then
    echo "✅ frame: $OUT"
else
    echo "❌ no screenshot (rc=$rc); see $BUILD/ra.log"
    exit 1
fi
