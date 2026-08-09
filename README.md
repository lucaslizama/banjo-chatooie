# Twitch Chat Integration — Banjo-Kazooie: Recompiled

Shows your Twitch chat on screen while you play.

## Two halves

Mod code compiles to MIPS and runs sandboxed inside the recomp runtime: no
sockets, no filesystem, no way to reach the network. So the mod is split in two.

| | what it is | what it does |
| --- | --- | --- |
| `native/` | a host shared library, `twitch_chat.so` | connects to Twitch IRC on its own thread, queues messages |
| `src/` | MIPS mod code, `twitch_chat_integration.nrm` | drains that queue once a frame and draws the overlay |

The MIPS side reaches the native side with `RECOMP_IMPORT(".", ...)` — `.` means
"an export of this mod itself", which is how the runtime routes a call into a
library the mod ships with. Every imported name must also appear in `mod.toml`'s
`native_libraries` entry.

**The two files are deployed side by side, not nested.** The runtime derives the
library filename from the manifest name (`twitch_chat` → `twitch_chat.so` on
Linux, `.dll` on Windows, `.dylib` on macOS) and looks for it in the folder
*containing* the `.nrm`. Putting it inside the `.nrm` does nothing.

## Build

```
./build.sh            # build/twitch_chat_integration.nrm + build/native/twitch_chat.so
./build.sh --deploy   # also copies both into ~/.config/BanjoRecompiled/mods
```

You need `clang` and `ld.lld` for the MIPS half, a host C++17 compiler for the
native half, and `tools/RecompModTool` from the
[N64Recomp releases page](https://github.com/N64Recomp/N64Recomp/releases)
(gitignored — it's a prebuilt binary).

The native library links libstdc++ statically, so it doesn't depend on the
player having a matching toolchain runtime.

## Using it

Enable the mod, open its options, and type your channel name into **Twitch
Channel** (no `#`). The overlay picks up the change within a couple of seconds —
no restart needed. Blank the field to disconnect.

Other options: how many lines to show, which corner, text size, background
opacity, and whether to hide `!command` messages aimed at chat bots.

## About the connection

Twitch allows anonymous read access to any public channel's chat, so the mod logs
in as `justinfan<random>` with no password.

- **No credentials.** The mod never asks for a Twitch account, password, or OAuth
  token, and there are none in this repo. There should never be.
- **Read-only.** It cannot send messages, and it joins exactly one channel.
- **Nothing is stored.** Messages live in a small in-memory queue and are dropped
  once they scroll off the overlay.

Chat is arbitrary user input, so messages are sanitized before they reach the UI:
control characters stripped, whitespace collapsed, length capped, and truncation
kept on UTF-8 boundaries.

The connection uses Twitch's plaintext IRC port (6667) rather than TLS (6697).
Nothing secret crosses it — the login is anonymous and the content is already
public — and avoiding TLS keeps an OpenSSL dependency out of a library that has
to be shipped prebuilt for three platforms. If Twitch ever drops plaintext IRC,
this is the thing that will break first.

## How it hooks into the game

`recomp_on_init` is the only event the recomp runtime declares, and it fires once
at boot — the mod uses it to open the connection. For a per-frame tick the mod
hooks `mainLoop`, Banjo-Kazooie's own frame function
(`bk-decomp/src/core1/code_0.c`), which is called from `mainThread_entry`'s loop.

The overlay is built on the first frame rather than during `recomp_on_init`, so
the UI system is definitely up before any `recompui` call happens.

Each frame the tick drains at most three messages. A busy channel can arrive
faster than that, but draining the whole queue in one frame would let chat
dictate frame time.

## Layout

```
mod.toml            manifest: config options, native library declaration
Makefile            MIPS build
mod.ld              MIPS linker script
build.sh            builds both halves, packages, optionally deploys
include/            recomp API headers + the shared MIPS/native contract
src/                MIPS mod code (main.c, overlay.c)
native/             host library (irc_client.cpp, twitch_chat.cpp, recomp_abi.h)
```

`native/recomp_abi.h` is a hand-written copy of the parts of the runtime's ABI
this library needs — the `recomp_context` layout, the rdram address translation
with its byteswap, and argument/return helpers. librecomp isn't distributed as an
SDK, so there is nothing to include; the definitions there were taken from
`N64Recomp/include/recomp.h` and `librecomp/include/librecomp/helpers.hpp`.
