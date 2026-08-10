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
for arg in "$@"; do
    [ "$arg" = "--publish" ] && PUBLISH=1
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

NAME="twitch-chat-integration-v${VERSION}-${PLATFORM}"
STAGE="dist/$NAME"

echo "==> building"
./build.sh

echo "==> staging $NAME"
rm -rf "$STAGE" "dist/$NAME.zip"
mkdir -p "$STAGE/helper"

cp "build/twitch_chat_integration.nrm" "$STAGE/"
cp "build/native/$LIB" "$STAGE/"
cp README.md LICENSE "$STAGE/"
cp helper/twitch_redemptions.py helper/README.md "$STAGE/helper/"

# A short note for people who unzip it and want to know what to do, without
# reading the whole README first.
cat > "$STAGE/INSTALL.txt" <<EOF
Twitch Chat Integration v${VERSION} for Banjo-Kazooie: Recompiled
${PLATFORM}

1. Copy these two files into your mods folder:

       twitch_chat_integration.nrm
       ${LIB}

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

(cd dist && zip -qr "$NAME.zip" "$NAME")
rm -rf "$STAGE"

SIZE=$(du -h "dist/$NAME.zip" | cut -f1)
SHA=$(sha256sum "dist/$NAME.zip" | cut -d' ' -f1)

echo
echo "dist/$NAME.zip  ($SIZE)"
echo "sha256  $SHA"

if [ "$PUBLISH" -eq 1 ]; then
    TAG="v$VERSION"
    if gh release view "$TAG" >/dev/null 2>&1; then
        echo "==> uploading to the existing $TAG release"
        gh release upload "$TAG" "dist/$NAME.zip" --clobber
    else
        echo "==> creating release $TAG"
        gh release create "$TAG" "dist/$NAME.zip" \
            --title "Twitch Chat Integration $TAG" \
            --notes-file RELEASE_NOTES.md
    fi
    gh release view "$TAG" --json url --jq .url
fi
