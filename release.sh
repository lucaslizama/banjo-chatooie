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
for arg in "$@"; do
    case "$arg" in
        --publish) PUBLISH=1 ;;
        --windows) WINDOWS=1 ;;
    esac
done

VERSION=$(sed -n 's/^version = "\(.*\)"/\1/p' mod.toml | head -1)
if [ -z "$VERSION" ]; then
    echo "Could not read version from mod.toml" >&2
    exit 1
fi

case "$(uname -s)" in
    Linux)  PLATFORM="linux-$(uname -m)"; LIB="twitch_chat.so"    ;;
    Darwin) PLATFORM="macos-$(uname -m)"; LIB="twitch_chat.dylib" ;;
    *)      PLATFORM="windows-x86_64";    LIB="twitch_chat.dll"   ;;
esac

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
    local name="twitch-chat-integration-v${VERSION}-${platform}"
    local stage="dist/$name"

    echo "==> staging $name"
    rm -rf "$stage" "dist/$name.zip"
    mkdir -p "$stage/helper"

    cp "build/twitch_chat_integration.nrm" "$stage/"
    cp "$libdir/$lib" "$stage/"
    cp README.md LICENSE "$stage/"
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
Twitch Chat Integration v${VERSION} for Banjo-Kazooie: Recompiled
${platform}

1. Copy these two files into your mods folder:

       twitch_chat_integration.nrm
       ${lib}

   Linux:   ~/.config/BanjoRecompiled/mods
   Windows: %APPDATA%\\BanjoRecompiled\\mods
   macOS:   ~/Library/Application Support/BanjoRecompiled/mods

   Both files, in the same folder, next to each other. The mod will not load
   without the second one.

2. Start the game, open the mod menu and enable Twitch Chat Integration.

3. Open its options and type your channel name into Twitch Channel, without
   the '#'.

Chat appears in the corner within a couple of seconds. To have game characters
read messages aloud, someone in chat types:

       !say bottles: hello there

See README.md for every character name and every option, and helper/README.md
if you want channel point redemptions.
EOF
}

echo
package "$PLATFORM" "$LIB" "build/native"
if [ "$WINDOWS" -eq 1 ]; then
    package "windows-x86_64" "twitch_chat.dll" "build/native-windows"
fi

if [ "$PUBLISH" -eq 1 ]; then
    TAG="v$VERSION"
    ARCHIVES=(dist/twitch-chat-integration-v${VERSION}-*.zip)

    if gh release view "$TAG" >/dev/null 2>&1; then
        echo "==> uploading to the existing $TAG release"
        gh release upload "$TAG" "${ARCHIVES[@]}" --clobber
    else
        echo "==> creating release $TAG"
        gh release create "$TAG" "${ARCHIVES[@]}" \
            --title "Twitch Chat Integration $TAG" \
            --notes-file RELEASE_NOTES.md
    fi
    gh release view "$TAG" --json url --jq .url
fi
