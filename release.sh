#!/usr/bin/env bash
# Packages a release someone else can download and drop into their mods folder.
#
# The version comes from mod.toml, so it is set in exactly one place.
#
# Note the platform suffix on the archive name. The .nrm is MIPS code and works
# anywhere, but the native library is a host binary and the mod will not load
# without one for the player's platform. Building on Windows or macOS produces
# the same layout with a .dll or .dylib in place of the .so.
#
# Usage:
#   ./release.sh              build the archive into dist/
#   ./release.sh --publish    also create the GitHub release and upload it
set -euo pipefail

cd "$(dirname "$0")"

PUBLISH=0
WINDOWS=0
THUNDERSTORE=0
for arg in "$@"; do
    case "$arg" in
        --publish)     PUBLISH=1 ;;
        --windows)     WINDOWS=1 ;;
        --thunderstore) THUNDERSTORE=1; WINDOWS=1 ;;   # one package covers both
    esac
done

# Package identity on Thunderstore. Letters, digits and underscores only.
TS_NAME="BanjoChatooie"
TS_WEBSITE="https://github.com/lucaslizama/banjo-chatooie"
# Thunderstore shows this above everything else, so it has to stay true. It said
# "BETA, and it can still crash" until 0.3.0, which was honest at the time and
# became stale the moment the crash was fixed. Revisit it at every release.
TS_DESCRIPTION="Your Twitch chat appears in game, and characters read messages aloud in Banjo-Kazooie's own text boxes, with their portraits and voices. Chat picks who speaks. Beta, but the 0.2.0 crash is fixed."

VERSION=$(sed -n 's/^version = "\(.*\)"/\1/p' mod.toml | head -1)
if [ -z "$VERSION" ]; then
    echo "Could not read version from mod.toml" >&2
    exit 1
fi

case "$(uname -s)" in
    Linux)  PLATFORM="linux-$(uname -m)"; LIB="banjo_chatooie_native.so"    ;;
    Darwin) PLATFORM="macos-$(uname -m)"; LIB="banjo_chatooie_native.dylib" ;;
    *)      PLATFORM="windows-x86_64";    LIB="banjo_chatooie_native.dll"   ;;
esac

# Start from an empty dist/, always.
#
# Archives are named by version, so stale ones accumulate silently and every one
# of them looks publishable. That is not hypothetical: a v0.2.1 archive survived a
# version bump that was reconsidered, leaving a file on disk for a release that
# never existed and must never be uploaded. Old artifacts are also unhelpful in
# the other direction -- a build that fails after the zip step leaves the previous
# attempt sitting there looking like a success.
#
# Everything here is reproducible from a tag, so nothing in dist/ is worth
# keeping.
if [ -d dist ]; then
    echo "==> clearing dist/ ($(find dist -maxdepth 1 -name '*.zip' | wc -l) old archive(s))"
    rm -rf dist
fi
mkdir -p dist

echo "==> building"
./build.sh

if [ "$WINDOWS" -eq 1 ]; then
    if ! command -v x86_64-w64-mingw32-g++ >/dev/null; then
        echo "mingw-w64 not installed; cannot cross-compile the Windows library." >&2
        echo "  sudo pacman -S --needed mingw-w64-gcc" >&2
        exit 1
    fi
    echo "==> building the Windows library"
    make -C native TARGET_OS=windows -j"$(nproc)"
fi

# Builds one archive. The .nrm is the same in every one; only the native library
# and the install instructions differ, since the runtime will not load the mod
# without a library matching the player's platform.
package() {
    local platform="$1" lib="$2" libdir="$3"
    local name="banjo-chatooie-v${VERSION}-${platform}"
    local stage="dist/$name"

    echo "==> staging $name"
    rm -rf "$stage" "dist/$name.zip"
    mkdir -p "$stage/helper"

    cp "build/banjo_chatooie.nrm" "$stage/"
    cp "$libdir/$lib" "$stage/"
    cp README.md LICENSE NOTICE "$stage/"
    cp helper/twitch_redemptions.py helper/README.md "$stage/helper/"

    write_install_note "$stage" "$platform" "$lib"

    (cd dist && zip -qr "$name.zip" "$name")
    rm -rf "$stage"

    echo "dist/$name.zip  ($(du -h "dist/$name.zip" | cut -f1))"
    echo "  sha256  $(sha256sum "dist/$name.zip" | cut -d' ' -f1)"
}

# A short note for people who unzip it and want to know what to do, without
# reading the whole README first.
write_install_note() {
    local stage="$1" platform="$2" lib="$3"
    cat > "$stage/INSTALL.txt" <<EOF
Banjo-Chatooie v${VERSION} for Banjo-Kazooie: Recompiled
${platform}

1. Copy these two files into your mods folder:

       banjo_chatooie.nrm
       ${lib}

   Linux:   ~/.config/BanjoRecompiled/mods
   Windows: %APPDATA%\\BanjoRecompiled\\mods
   macOS:   ~/Library/Application Support/BanjoRecompiled/mods

   Both files, in the same folder, next to each other. The mod will not load
   without the second one.

2. Start the game, open the mod menu and enable Banjo-Chatooie.

3. Open its options and type your channel name into Twitch Channel, without
   the '#'.

Chat appears in the corner within a couple of seconds. To have game characters
read messages aloud, someone in chat types:

       !say bottles: hello there

See README.md for every character name and every option, and helper/README.md
if you want channel point redemptions.
EOF
}

# Thunderstore packages are laid out flat at the zip root, and the mod manager
# extracts them straight into the mods folder. That suits this mod: the runtime
# picks its native library by platform extension, so shipping the .so and the
# .dll side by side means one package works on both.
package_thunderstore() {
    local name="banjo-chatooie-thunderstore-v${VERSION}"
    local stage="dist/$name"

    echo "==> staging $name"
    rm -rf "$stage" "dist/$name.zip"
    mkdir -p "$stage/helper"

    cp build/banjo_chatooie.nrm "$stage/"
    cp build/native/banjo_chatooie_native.so "$stage/"
    cp build/native-windows/banjo_chatooie_native.dll "$stage/"
    cp README.md LICENSE NOTICE "$stage/"
    cp assets/thunderstore-icon.png "$stage/icon.png"
    cp helper/twitch_redemptions.py helper/README.md "$stage/helper/"

    cat > "$stage/manifest.json" <<EOF
{
    "name": "${TS_NAME}",
    "version_number": "${VERSION}",
    "website_url": "${TS_WEBSITE}",
    "description": "${TS_DESCRIPTION}",
    "dependencies": []
}
EOF

    # Everything below is rejected at upload time rather than at build time, so
    # it is much cheaper to catch here.
    local problems=0
    if ! printf '%s' "$TS_NAME" | grep -qE '^[A-Za-z0-9_]+$'; then
        echo "  name must be letters, digits and underscores only" >&2; problems=1
    fi
    if ! printf '%s' "$VERSION" | grep -qE '^[0-9]+\.[0-9]+\.[0-9]+$'; then
        echo "  version must be major.minor.patch, got '$VERSION'" >&2; problems=1
    fi
    if [ "${#TS_DESCRIPTION}" -gt 250 ]; then
        echo "  description is ${#TS_DESCRIPTION} characters, limit is 250" >&2; problems=1
    fi
    if [ "$(magick identify -format '%wx%h' "$stage/icon.png" 2>/dev/null)" != "256x256" ]; then
        echo "  icon.png must be exactly 256x256" >&2; problems=1
    fi
    python3 -c "import json,sys; json.load(open('$stage/manifest.json'))" || problems=1
    [ "$problems" -eq 0 ] || { echo "Thunderstore package would be rejected." >&2; exit 1; }

    # Zipped from inside, so the files land at the archive root with no wrapper
    # directory. Thunderstore requires that.
    (cd "$stage" && zip -qr "../$name.zip" .)
    rm -rf "$stage"

    echo "dist/$name.zip  ($(du -h "dist/$name.zip" | cut -f1))"
    echo "  upload at https://thunderstore.io/c/banjo-recompiled/create/"
}

echo
if [ "$THUNDERSTORE" -eq 1 ]; then
    package_thunderstore
else
    package "$PLATFORM" "$LIB" "build/native"
    if [ "$WINDOWS" -eq 1 ]; then
        package "windows-x86_64" "banjo_chatooie_native.dll" "build/native-windows"
    fi
fi

if [ "$PUBLISH" -eq 1 ]; then
    TAG="v$VERSION"
    ARCHIVES=(dist/banjo-chatooie-v${VERSION}-*.zip)

    if gh release view "$TAG" >/dev/null 2>&1; then
        echo "==> uploading to the existing $TAG release"
        gh release upload "$TAG" "${ARCHIVES[@]}" --clobber
    else
        echo "==> creating release $TAG"
        gh release create "$TAG" "${ARCHIVES[@]}" \
            --title "Banjo-Chatooie $TAG" \
            --prerelease \
            --notes-file RELEASE_NOTES.md
    fi
    gh release view "$TAG" --json url --jq .url
fi
