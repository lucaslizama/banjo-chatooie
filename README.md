# Twitch Chat Integration — Banjo-Kazooie: Recompiled

Shows your Twitch chat on screen while you play, and lets chat put words in the
game characters' mouths.

Two things happen with an incoming message:

- it scrolls in an overlay panel in the corner of the screen, and
- if it triggers the speak feature, a character says it in one of the game's own
  dialogue boxes, with that character's portrait and voice samples.

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

## Making characters speak

**Characters Speak On** picks the trigger:

| Mode | Requirement |
| --- | --- |
| `!say command` (default) | none — works on any channel |
| `Highlight My Message` | Twitch Affiliate, since it's a channel points reward |
| `Every message` | none — chaotic, useful for testing |
| `Off` | — |

Whoever is speaking is chosen by a `name:` prefix, so `!say mumbo: hello banjo`
comes out of Mumbo. An unrecognised name (or none at all) falls back to the
**Default Character** setting. Around fifty names are mapped — banjo, kazooie,
bottles, mumbo, grunty, tooty, brentilda, cheato, klungo, boggy, wozza, gobi,
rubee, tiptup, clanker, snacker, the five jinjo colours, and more.

**Who Can Use !say** restricts the command to you and your moderators; `!say` is
exempt from the "Hide Bot Commands" filter either way.

Two things about the game's dialogue system are worth knowing. Its font has no
lowercase glyphs — real assets store text like `DINGPOT, DINGPOT` — so spoken
text is upper-cased on the way in, while the overlay keeps the original casing.
And only portraits `0x0C` and up can be addressed, because the blob's portrait
byte maps to `cmd + 0xC`; every major speaking character is above that line.

## How the dialogue hijack works

`gcdialog_showDialog(text_id, …)` leads to `loadDialogStrings`, which parses a
byte blob returned by `dialogBin_get`:

```
[count][cmd][len][text NUL] x count      <- bottom box
[count][cmd][len][text NUL] x count      <- top box
```

`cmd` selects the portrait (`gczoombox_new` receives `cmd + 0xC`), and the same
id picks the character's voice samples, since `__gczoombox_load_sfx` reads both
out of `D_8036C6C0[portrait_id]`. Choosing a character gets the face and the
voice together. `len` counts the stored bytes including the terminator, and the
portrait byte carries the high bit so it can't be mistaken for one of the
commands in the `0x01`–`0x1F` range.

The mod patches `dialogBin_get`, reimplements the original faithfully, and
substitutes its own blob for exactly one call. That call is carried on a **real**
asset id rather than an invented one: `dialogBin_release` frees whatever
`s_dialogBin.ptr` points at, so an id the asset cache never loaded would
unbalance it.

Each box's entry list **must** end with a close command (`cmd` `-4`). Without it
the state machine advances past the end of the array `loadAndCreateDialogs`
allocated, reads whatever follows as the next portrait and string, and leaves
that box open forever — which then blocks every later conversation in the game,
since `gcdialog_hasCurrentTextId()` never goes false again.

Long messages need no splitting: the zoombox wraps and scrolls by itself via
`_gczoombox_findLineBreak` at 24 characters per line.

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
src/                MIPS mod code (main.c, overlay.c, speak.c)
native/             host library (irc_client.cpp, twitch_chat.cpp, recomp_abi.h)
```

`native/recomp_abi.h` is a hand-written copy of the parts of the runtime's ABI
this library needs — the `recomp_context` layout, the rdram address translation
with its byteswap, and argument/return helpers. librecomp isn't distributed as an
SDK, so there is nothing to include; the definitions there were taken from
`N64Recomp/include/recomp.h` and `librecomp/include/librecomp/helpers.hpp`.
