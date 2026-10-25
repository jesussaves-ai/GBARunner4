#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Host-side static analysis for the GBARunner4 core.
#
# Runs clang-tidy over code/core/{arm9,arm7} using flags equivalent to the
# build's, against the *host* devkitARM headers (libnds/libtwl). Third-party
# code (FatFS, tlsf, ArduinoJson) and assembly are skipped. This is a
# development aid only: it is NOT part of the build and never touches the ROM.
#
# Requirements: clang-tidy on PATH, and DEVKITPRO pointing at a devkitPro
# install that contains libnds (defaults to /opt/devkitpro).
#
# Usage:
#   ./lint.sh                     # lint the whole core (fails on findings)
#   ./lint.sh <files...>          # lint specific files
#   ./lint.sh --soft              # report only; never fail
#   ./lint.sh --checks '-*,bugprone-*'   # override the check set
#   CLANG_TIDY=clang-tidy-18 ./lint.sh   # pick a specific clang-tidy
# ---------------------------------------------------------------------------
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
REPORT="$ROOT/lint-report.txt"

# Locate clang-tidy: explicit CLANG_TIDY, then the unversioned name, then the
# newest versioned binary (CI runners often only ship clang-tidy-NN).
TIDY="${CLANG_TIDY:-}"
if [ -z "$TIDY" ]; then
    for c in clang-tidy clang-tidy-19 clang-tidy-18 clang-tidy-17 clang-tidy-16 clang-tidy-15 clang-tidy-14; do
        if command -v "$c" >/dev/null 2>&1; then TIDY="$c"; break; fi
    done
fi
if [ -z "$TIDY" ] || ! command -v "$TIDY" >/dev/null 2>&1; then
    echo "❌ clang-tidy not found on PATH (install clang-tools, or set CLANG_TIDY)." >&2
    exit 1
fi
if [ ! -d "$DEVKITPRO/libnds/include" ]; then
    echo "❌ libnds not found under '$DEVKITPRO' (set DEVKITPRO to your devkitPro root)." >&2
    exit 1
fi

# clang needs the devkitARM C and C++ headers for the arm-none-eabi target.
DEVKITARM="${DEVKITARM:-$DEVKITPRO/devkitARM}"
CXXINC="$(ls -d "$DEVKITARM"/arm-none-eabi/include/c++/*/ 2>/dev/null | sort -V | tail -1)"
if [ -z "$CXXINC" ]; then
    echo "❌ devkitARM C++ headers not found under '$DEVKITARM' (set DEVKITARM)." >&2
    exit 1
fi

CHECKS_OVERRIDE=""
SOFT=0
FILES=()
while [ $# -gt 0 ]; do
    case "$1" in
        --checks) CHECKS_OVERRIDE="$2"; shift 2 ;;
        --soft) SOFT=1; shift ;;
        -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
        *) FILES+=("$1"); shift ;;
    esac
done

COMMON_INC=(
    -I"$ROOT/code/core/common"
    -I"$ROOT/code/libs/mini-printf"
    -I"$DEVKITPRO/libnds/include"
    -I"$DEVKITPRO/libnds/include/nds"
    -I"$DEVKITPRO/calico/include"
    -isystem "$CXXINC"
    -isystem "${CXXINC}arm-none-eabi"
    -isystem "$DEVKITARM/arm-none-eabi/include"
)

# Note: GCC-only flags (-mthumb-interwork, -mtune=...) are omitted; clang
# rejects them, and they do not affect the analysis.
ARM9_FLAGS=(
    -target arm-none-eabi -marm -march=armv5te
    -D__NDS__ -DLIBTWL_ARM9 -DARM9 -DGBAR3_HICODE_CACHE_MAPPING
    -I"$ROOT/code/core/arm9/include"
    -I"$ROOT/code/core/arm9/source"
    -I"$ROOT/code/libs/libtwl/common/include"
    -I"$ROOT/code/libs/libtwl/libtwl9/include"
    "${COMMON_INC[@]}"
)

ARM7_FLAGS=(
    -target arm-none-eabi -march=armv4t
    -D__NDS__ -DLIBTWL_ARM7 -DARM7
    -I"$ROOT/code/core/arm7/source"
    -I"$ROOT/code/libs/libtwl/common/include"
    -I"$ROOT/code/libs/libtwl/libtwl7/include"
    "${COMMON_INC[@]}"
)

if [ ${#FILES[@]} -eq 0 ]; then
    while IFS= read -r f; do
        FILES+=("$f")
    done < <(find "$ROOT/code/core/arm9/source" "$ROOT/code/core/arm7/source" \
                  -type f \( -name '*.c' -o -name '*.cpp' \) | sort)
fi

: > "$REPORT"
echo "--- GBARunner4 core lint ($($TIDY --version 2>/dev/null | head -1)) ---"

clean=0; flagged=0; skipped=0
for f in "${FILES[@]}"; do
    case "$f" in
        */Fat/*|*tlsf.c|*ArduinoJson*|*ffunicode*|*ffsystem.c)
            skipped=$((skipped + 1)); continue ;;
    esac

    flags=("${ARM9_FLAGS[@]}")
    case "$f" in */arm7/*) flags=("${ARM7_FLAGS[@]}");; esac

    # Language-appropriate dialect (clang errors if a C++ -std is given for C).
    case "$f" in
        *.cpp) flags+=(-std=gnu++23 -fno-rtti -fno-exceptions -Wno-invalid-constexpr) ;;
        *)     flags+=(-std=gnu17) ;;
    esac

    args=()
    [ -n "$CHECKS_OVERRIDE" ] && args+=(--checks="$CHECKS_OVERRIDE")

    out="$("$TIDY" "$f" "${args[@]}" -- "${flags[@]}" 2>&1)"

    if printf '%s' "$out" | grep -q "Found compiler error"; then
        skipped=$((skipped + 1))
        printf 'SKIP  %s (parse error)\n' "${f#"$ROOT"/}" >> "$REPORT"
        continue
    fi

    n=$(printf '%s\n' "$out" | grep -cE '(warning|error):')
    if [ "${n:-0}" -gt 0 ]; then
        flagged=$((flagged + 1))
        printf '%-3d  %s\n' "$n" "${f#"$ROOT"/}"
        { printf '\n=== %s (%d)\n' "${f#"$ROOT"/}" "$n"; printf '%s\n' "$out"; } >> "$REPORT"
    else
        clean=$((clean + 1))
    fi
done

echo "---"
echo "clean: $clean   with findings: $flagged   skipped: $skipped"
if [ "$flagged" -gt 0 ]; then
    echo "Full findings: ${REPORT#"$ROOT"/}"
    if [ "$SOFT" -eq 0 ]; then
        echo "❌ $flagged file(s) with findings"
        exit 1
    fi
else
    rm -f "$REPORT"
fi
echo "✅ clang-tidy clean"
