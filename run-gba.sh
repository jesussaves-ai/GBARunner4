#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Headless GBA run + frame capture through a libretro core.
#
# Runs a .gba ROM in a libretro core for N frames and writes the last video
# frame to a PNG. Entirely headless: no X server, no GL, no window, so it can
# never disturb a desktop session. Uses the frame callback directly.
#
# Usage:
#   ./run-gba.sh <rom.gba> [frames] [out.png] [core.so]
#
# Env:
#   CORE        libretro core (default: /usr/lib/libretro/mgba_libretro.so)
#   LIBRETRO_H  path to libretro.h (else auto-located / downloaded once)
# ---------------------------------------------------------------------------
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="${TMPDIR:-/tmp}/lrshot-build"
SRC="$ROOT/tools/lrshot.c"
CORE="${CORE:-/usr/lib/libretro/mgba_libretro.so}"

ROM="${1:-}"
FRAMES="${2:-400}"
OUT="${3:-$BUILD/shot.png}"
[ $# -ge 4 ] && CORE="$4"

if [ -z "$ROM" ] || [ ! -f "$ROM" ]; then
    echo "usage: $0 <rom.gba> [frames] [out.png] [core.so]" >&2
    exit 2
fi

mkdir -p "$BUILD"

# Locate libretro.h (system, env, or download once into the build dir).
HDR="${LIBRETRO_H:-}"
if [ -z "$HDR" ]; then
    for c in /usr/include/libretro.h "$BUILD/libretro.h"; do
        [ -f "$c" ] && HDR="$c" && break
    done
fi
if [ -z "$HDR" ]; then
    HDR="$BUILD/libretro.h"
    curl -sfL -o "$HDR" \
        "https://raw.githubusercontent.com/libretro/libretro-common/master/include/libretro.h" \
        || { echo "❌ could not fetch libretro.h (set LIBRETRO_H)"; exit 1; }
fi

# Build the tiny frontend on demand.
BIN="$BUILD/lrshot"
if [ ! -x "$BIN" ] || [ "$SRC" -nt "$BIN" ] || [ "$HDR" -nt "$BIN" ]; then
    gcc -O2 -o "$BIN" "$SRC" -ldl -I"$(dirname "$HDR")" || exit 1
fi

[ -f "$CORE" ] || { echo "❌ core not found: $CORE"; exit 1; }

PPM="$BUILD/shot.ppm"
"$BIN" "$CORE" "$ROM" "$FRAMES" "$PPM"
rc=$?
if [ $rc -ne 0 ] || [ ! -s "$PPM" ]; then
    echo "❌ libretro run failed (rc=$rc)"
    exit 1
fi

if command -v convert >/dev/null 2>&1; then
    convert "$PPM" "$OUT"
elif command -v magick >/dev/null 2>&1; then
    magick "$PPM" "$OUT"
else
    cp "$PPM" "${OUT%.png}.ppm"
    OUT="${OUT%.png}.ppm"
fi

echo "✅ frame: $OUT"
