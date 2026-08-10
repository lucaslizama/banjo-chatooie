#!/usr/bin/env bash
# Build both halves of the mod and package them.
#
#   native/   host shared library (banjo_chatooie_native.so) -- talks to Twitch
#   src/      MIPS mod code (banjo_chatooie.nrm) -- draws the overlay
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
for candidate in build/native/banjo_chatooie_native.so build/native/banjo_chatooie_native.dylib build/native/banjo_chatooie_native.dll; do
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

    # Deploy by rename, never by overwriting in place.
    #
    # `cp` opens the destination O_TRUNC and rewrites the SAME inode. The game
    # mmaps the native library from that inode, so overwriting it while the game
    # is running rewrites the file under a live mapping: clean pages get dropped
    # and re-faulted from the new contents, and pages past the temporary EOF
    # fault outright. That is what crashed run 4c -- the .so was rewritten at
    # 13:51:49 and the game took a SIGSEGV at 13:51:50, rip=0, having called
    # through a relocation slot that read back as the file's own zeros.
    #
    # A rename swaps in a new inode instead. A running game keeps the old one
    # mapped, intact, until it exits.
    deploy() {
        local src="$1" dest="$MODS_DIR/$(basename "$1")"
        cp "$src" "$dest.tmp$$"
        mv -f "$dest.tmp$$" "$dest"
    }

    # Still worth saying out loud: the running game keeps the version it started
    # with, so a deploy mid-session is not the build under test.
    if pgrep -f BanjoRecompiled >/dev/null 2>&1; then
        echo "NOTE: the game is running. It keeps the previously deployed files"
        echo "      mapped until it exits -- restart it to pick this build up."
    fi

    deploy "$NRM"
    deploy "$LIB"
    echo "Deployed to: $MODS_DIR/$(basename "$NRM")"
    echo "             $MODS_DIR/$(basename "$LIB")"
fi
