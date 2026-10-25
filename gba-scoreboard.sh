#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Headless GBA test-ROM scoreboard.
#
# Runs every .gba under a directory through a libretro core (via run-ds.sh,
# which uses RetroArch with null video/audio drivers -- no X, no GL, no window)
# and saves one screenshot per ROM. Prints the unique-colour count as a first
# pass; a solid image is a pass, black-on-white text is a failure message.
#
# Usage:
#   ./gba-scoreboard.sh <rom-dir> [outdir] [core.so]
#
# Env:
#   FRAMES   frames per ROM          (default 300)
#   SYS_DIR  system dir with the GBA BIOS (e.g. /path/to/BIOS)
# ---------------------------------------------------------------------------
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DIR="${1:-}"
OUT="${2:-${TMPDIR:-/tmp}/gba-score}"
CORE="${3:-/usr/lib/libretro/mgba_libretro.so}"
FRAMES="${FRAMES:-300}"

if [ -z "$DIR" ] || [ ! -d "$DIR" ]; then
    echo "usage: $0 <rom-dir> [outdir] [core.so]" >&2
    exit 2
fi
mkdir -p "$OUT"

for rom in $(find "$DIR" -name '*.gba' | sort); do
    name="$(echo "$rom" | sed "s#$DIR/##; s#/#_#g; s#\.gba##")"
    "$ROOT/run-ds.sh" "$rom" "$FRAMES" "$OUT/$name.png" "$CORE" >/dev/null 2>&1
    colors="$(identify -format '%k' "$OUT/$name.png" 2>/dev/null || echo FAIL)"
    printf '%-24s %s colors  %s\n' "$name" "$colors" "$OUT/$name.png"
done
