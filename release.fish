#!/usr/bin/env fish
# ---------------------------------------------------------------------------
# GBARunner4 release helper.
#
# Builds the ROM (in the pinned devkitARM container, exactly like
# `./verify.fish`), packages it with the TWiLight Menu++ / flashcart / DSpico
# layout, and (optionally) publishes it as a GitHub Release.
#
# The build output keeps the historic `GBARunner3.nds` filename and the
# `_gba/configs/` folder so existing launchers keep working, while the NDS
# banner/title metadata identifies the binary as GBARunner4 by Jesus Saves.
#
# Usage:
#   ./release.fish              # build + package only (no publish)
#   ./release.fish v4.0.0       # build + package + create/upload a GitHub release
#
# Requirements: Docker (for the build) and, to publish, an authenticated
# GitHub CLI (`gh auth login`).
# ---------------------------------------------------------------------------

set -l tag $argv[1]
set -l rom code/bootstrap/GBARunner3.nds
set -l zip GBARunner4.zip

echo "--- Building GBARunner4 ---"
docker run --rm -v (pwd):/gba devkitpro/devkitarm:20230928 bash -c "
    cd /gba/code && make -C bootstrap
"
if test $status -ne 0
    echo "❌ Build failed!"
    exit 1
end

if not test -f $rom
    echo "❌ $rom not found after build!"
    exit 1
end

# Package with the layout the loader expects: GBARunner3.nds at the root and
# per-game configs under _gba/configs/ (see
# GAME_SETTINGS_FILE_PATH_FORMAT in code/core/arm9/source/main.cpp).
rm -rf out
mkdir -p out/_gba
cp -f $rom out/
cp -r configs out/_gba/

rm -f $zip
cd out
zip -qr ../$zip .
cd ..

echo "✅ Packaged $zip"
echo "   out/GBARunner3.nds         (flashcart / DSpico / TWiLight Menu++)"
echo "   out/_gba/configs/*.json    (per-game JIT configs)"

if test -z "$tag"
    echo ""
    echo "To publish to GitHub Releases, run:"
    echo "  ./release.fish v4.0.0"
    echo "or manually:"
    echo "  gh release create $tag out/GBARunner3.nds $zip --title \"GBARunner4 $tag\" --notes \"...\""
    exit 0
end

echo "--- Publishing GitHub Release $tag ---"
gh release create $tag out/GBARunner3.nds $zip \
    --title "GBARunner4 $tag" \
    --notes "GBARunner4 $tag — ARM9/ARM7 GBA hypervisor for Nintendo DS / DSi / 3DS. Launch GBARunner3.nds from a slot-1 flashcart, DSpico, or TWiLight Menu++."

if test $status -ne 0
    echo "❌ Release publish failed (is the GitHub CLI authenticated?)"
    exit 1
end
echo "✅ Published $tag"
