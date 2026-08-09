#!/usr/bin/env bash
# Build both halves of the mod and package them.
#
#   native/   host shared library (twitch_chat.so) -- talks to Twitch
#   src/      MIPS mod code (twitch_chat_integration.nrm) -- draws the overlay
#
# The runtime looks for the native library in the folder CONTAINING the .nrm, so
# the two files have to be deployed side by side. --deploy copies both.
#
# Usage:
#   ./build.sh            build + package
#   ./build.sh --deploy   build + package + install into MODS_DIR
set -euo pipefail

cd "$(dirname "$0")"

MOD_TOOL="${MOD_TOOL:-./tools/RecompModTool}"
MODS_DIR="${MODS_DIR:-$HOME/.config/BanjoRecompiled/mods}"

DEPLOY=0
MAKE_ARGS=()
for arg in "$@"; do
    case "$arg" in
        --deploy) DEPLOY=1 ;;
        *) MAKE_ARGS+=("$arg") ;;
    esac
done

if [ ! -x "$MOD_TOOL" ]; then
    echo "RecompModTool not found at $MOD_TOOL" >&2
    echo "Download it from https://github.com/N64Recomp/N64Recomp/releases" >&2
    exit 1
fi

JOBS="$(nproc)"

echo "==> native library"
make -C native -j"$JOBS" ${MAKE_ARGS[@]+"${MAKE_ARGS[@]}"}

echo "==> mod code"
make -j"$JOBS" ${MAKE_ARGS[@]+"${MAKE_ARGS[@]}"}

echo "==> packaging"
"$MOD_TOOL" mod.toml build

NRM=$(ls build/*.nrm)

LIB=""
for candidate in build/native/twitch_chat.so build/native/twitch_chat.dylib build/native/twitch_chat.dll; do
    if [ -f "$candidate" ]; then
        LIB="$candidate"
        break
    fi
done

if [ -z "$LIB" ]; then
    echo "Native library missing from build/native -- the mod will fail to load." >&2
    exit 1
fi

echo
echo "Built: $NRM"
echo "       $LIB"

if [ "$DEPLOY" -eq 1 ]; then
    mkdir -p "$MODS_DIR"
    cp "$NRM" "$MODS_DIR/"
    cp "$LIB" "$MODS_DIR/"
    echo "Deployed to: $MODS_DIR/$(basename "$NRM")"
    echo "             $MODS_DIR/$(basename "$LIB")"
fi
