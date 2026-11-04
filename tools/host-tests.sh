#!/usr/bin/env bash
# Host-side tests for pure logic that does not need DS hardware.
# Usage: ./tools/host-tests.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CXX="${CXX:-g++}"
BUILD="${TMPDIR:-/tmp}/gbarunner4-host-tests"

mkdir -p "$BUILD"
"$CXX" -std=c++17 -I"$ROOT/code/core/arm9/source" \
    "$ROOT/code/test/host/save_path_test.cpp" \
    -o "$BUILD/save_path_test"
"$BUILD/save_path_test"
echo "✅ host tests passed"
