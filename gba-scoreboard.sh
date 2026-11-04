#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Headless GBA test-ROM scoreboard.
#
# Builds the embedded-ROM GBARunner test NDS for each .gba under a directory and
# runs it through tools/headless-test.sh. That script reads the guest report from
# DS main RAM, so the scoreboard reflects the hypervisor's result instead of a
# screenshot of an unrelated DS core.
#
# Usage:
#   ./gba-scoreboard.sh <rom-dir> [outdir] [core.so]
#
# Env:
#   FRAMES   frames per ROM          (default 300)
# ---------------------------------------------------------------------------
set -u
set -o pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DIR="${1:-}"
OUT="${2:-${TMPDIR:-/tmp}/gba-score}"
CORE="${3:-/usr/lib/libretro/desmume_libretro.so}"
FRAMES="${FRAMES:-300}"

if [ -z "$DIR" ] || [ ! -d "$DIR" ]; then
    echo "usage: $0 <rom-dir> [outdir] [core.so]" >&2
    exit 2
fi
if [ ! -f "$CORE" ]; then
    echo "❌ libretro core not found: $CORE" >&2
    exit 2
fi
mkdir -p "$OUT"

while IFS= read -r -d '' rom; do
    rel="${rom#"$DIR"/}"
    name="${rel%.gba}"
    name="${name//\//_}"
    outdir="$OUT/$name"
    mkdir -p "$outdir"
    OUTDIR="$outdir" FRAMES="$FRAMES" "$ROOT/tools/headless-test.sh" "$rom" "$CORE" \
        >"$outdir/log" 2>&1
    rc=$?
    if [ "$rc" -eq 0 ]; then
        status=PASS
    else
        status="FAIL($rc)"
    fi
    printf '%-24s %s  %s\n' "$name" "$status" "$outdir/frame.ppm"
done < <(find "$DIR" -name '*.gba' -print0 | sort -z)
