# Developing

Notes for working on the mod itself. If you only want to use it, the README is
enough.

## Two halves

Mod code compiles to MIPS and runs sandboxed inside the recomp runtime. It has no
sockets and no filesystem, so anything that touches the network has to live
outside it. The mod is therefore split in two:

| | what it is | what it does |
| --- | --- | --- |
| `native/` | a host shared library, `banjo_chatooie_native.so` | connects to Twitch IRC on its own thread, queues messages |
| `src/` | MIPS mod code, `banjo_chatooie.nrm` | drains that queue once a frame, draws the overlay, drives the text boxes |

The MIPS side reaches the native side with `RECOMP_IMPORT(".", ...)`. The `.`
means "an export of this mod itself", which is how the runtime routes a call into
a library the mod ships with. Every imported name must also appear in `mod.toml`
under `native_libraries`, or the mod fails to load with an invalid import error.

The two files are deployed side by side, not nested. The runtime builds the
library filename from the manifest name (`twitch_chat` becomes `banjo_chatooie_native.so`
on Linux, `.dll` on Windows, `.dylib` on macOS) and looks for it in the folder
*containing* the `.nrm`. Putting it inside the `.nrm` does nothing at all.

Channel point redemptions are a third piece, `helper/twitch_redemptions.py`,
described in `helper/README.md`. They are not carried over IRC, so they arrive
through a loopback socket instead.

## Building

```
./build.sh            # builds both halves and packages the .nrm
./build.sh --deploy   # also copies both files into ~/.config/BanjoRecompiled/mods
```

You need `clang` and `ld.lld` for the MIPS half, a host C++17 compiler for the
native half, and `tools/RecompModTool` from the
[N64Recomp releases page](https://github.com/N64Recomp/N64Recomp/releases). That
last one is gitignored, being a prebuilt binary.

The native library links libstdc++ statically so players do not need a matching
toolchain runtime.

## Releasing

Bump `version` in `mod.toml`, then push a tag that matches it:

```
git tag v0.2.0 && git push origin v0.2.0
```

A GitHub Action builds both platforms and publishes them. It refuses to run if
the tag and the manifest version disagree, which is the mistake that is easy to
make and annoying to undo once a release exists.

The same thing can be done locally:

```
./release.sh                        # host archive only
./release.sh --windows              # also cross-compiles the DLL
./release.sh --windows --publish    # and uploads both to the matching release
./release.sh --thunderstore         # one package for Thunderstore instead
```

The project is CC BY-SA 4.0; `NOTICE` records that and the CC0 status of the
vendored recomp headers and submodules.

```
```

Release notes come from `RELEASE_NOTES.md`, so edit that before tagging.

There is no macOS archive. Cross-compiling for macOS needs the Xcode SDK, which
Apple does not permit redistributing, so `.dylib` builds have to happen on a Mac.
Running `./release.sh` there produces one from the same source.

## How the mod gets a per-frame tick

The recomp runtime declares exactly one event of its own, `recomp_on_init`, which
fires once at boot. There is no per-frame event, so the mod hooks the base game
instead: `RECOMP_HOOK("mainLoop")`, Banjo-Kazooie's own frame function in
`bk-decomp/src/core1/code_0.c`, called from `mainThread_entry`'s loop.

The overlay is built on the first frame rather than during `recomp_on_init`, so
the UI system is definitely up before any `recompui` call happens.

Each frame the tick drains at most three messages. A busy channel can arrive
faster than that, but draining the whole queue in one frame would let chat
dictate frame time.

## Driving the game's text boxes

`gcdialog_showDialog(text_id, ...)` leads to `loadDialogStrings`, which parses a
byte blob returned by `dialogBin_get`:

```
[count][cmd][len][text NUL] x count      <- bottom box
[count][cmd][len][text NUL] x count      <- top box
```

`cmd` selects the portrait (`gczoombox_new` receives `cmd + 0xC`), and the same
id picks the character's voice samples, since `__gczoombox_load_sfx` and
`__gczoombox_load_sprite` both index `D_8036C6C0[portrait_id]`. Choosing a
character gets the face and the voice together, for free.

`len` counts the stored bytes including the terminator. The portrait byte carries
the high bit so it cannot be mistaken for one of the commands in the `0x01` to
`0x1F` range. All of this was confirmed against real asset `0x0E57`, whose first
entry reads:

```
07 B5 1F "DINGPOT, DINGPOT BY THE BENCH," 00
   |  |  30 characters plus the NUL is 31, which is 0x1F
   |  portrait: 0xB5 - 0x80 = 0x35, plus 0xC is 0x41
   seven entries in this box
```

The mod patches `dialogBin_get`, reimplements the original faithfully, and
substitutes its own blob for exactly one call. That call rides on a *real* asset
id rather than an invented one, because `dialogBin_release` frees whatever
`s_dialogBin.ptr` points at, and an id the asset cache never loaded would
unbalance it.

## Engine landmines

Four things in the original game will softlock or crash it the moment you feed
arbitrary text through the dialogue system. None of them can be hit by Rare's own
dialogue, which is why they were never found.

**Every entry list needs a terminator after the last text entry.** The `default:`
branch of `dialog_update` reads `CMD(string_index + 1)->cmd` for every non-empty
text entry, checking for the `-8` and `-9` conditional markers, so the game always
looks one entry past the one it is showing. Without an entry there it reads off
the end of the array `loadAndCreateDialogs` allocated, treats the garbage as the
next portrait and string, and leaves that box open forever. After that
`gcdialog_hasCurrentTextId()` never goes false again and no conversation anywhere
in the game can start.

**No word may be longer than the line.** `_gczoombox_findLineBreak` scans
backwards for a space that fits inside 24 printed characters, and its loop has no
lower bound on the index. A longer word runs the index negative, off the front of
the string, until some earlier byte happens to equal `0x20`, and it returns a
negative length that the caller writes through as `unk60[negative] = 0`. The mod
forces a break into any run over 18 characters.

**Nothing above `0x7E` may reach the box.** `0xFD` is an escape the printer
consumes along with the byte after it, and the rest index off the end of the
font. A message containing accented letters crashed the game outright. Text is
decoded as UTF-8 and folded to printable ASCII before it goes anywhere near the
blob.

**Never inject during a cutscene.** Cutscenes drive the dialogue system
themselves, and starting a box in a gap between their lines corrupts it. The mod
refuses on cutscene maps (`0x1E` to `0x20`, `0x7B` to `0x8A`, and everything from
`0x91` up) and waits for 30 consecutive idle frames first.

One more, less dangerous but easy to trip over: `loadDialogStrings` copies the
portrait and length bytes into its own allocation, but keeps `str` as a pointer
straight into the blob for as long as the box is on screen. The blob is double
buffered because of it, or building the next message would rewrite live text
underneath the current one.

Long messages need no splitting, though. The zoombox wraps and scrolls by itself
at 24 characters per line.

## The portrait numbering is wrong in the decomp

`GcZoomboxSprite` in the decompilation names 106 portraits. The real table has
107. The size is recoverable from the symbol file: `D_8036D924 - D_8036C6C0` is
4708 bytes, and `gczoomboxPortraitInfo` is 44 bytes (`s16 + s8 + s8` plus five
8-byte sound entries), which divides exactly into 107.

So one portrait is missing from the enum, and every label after the gap sits one
index too low. The entry labelled `BANJO_3` draws Tooty; `KAZOOIE_3` draws Banjo.
Grunty at `0x41` is correct and the Dingpot at `0x64` is one higher than its
label, which places the missing entry somewhere between `0x42` and `0x5F`.

Entries in `kPortraits` marked `verified` were checked against what the game
actually renders. The rest are still the enum's values and may be off by one. The
`#<index>` syntax exists to check one: `!say #87: test` and see who turns up.

## Booting straight into a map

`Debug: Boot To Map` skips the intro and file select. It works by patching
`getDefaultBootMap`, not by redirecting the boot afterwards, because `core1_init`
calls `setBootMap(getDefaultBootMap())` *and* `func_8023DA9C(3)` before
`mainThread_entry` ever enters its `while (1) mainLoop()` loop. Any per-frame
hook is far too late.

No save file is loaded when booting this way, so the world comes up as an empty
file. That is fine for testing and matches what the game's own debug boot does.

## Seeing the mod's log output

`recomp_printf` goes to stdout. Redirected to a file, libc block buffers it and
nothing appears until about 4KB accumulates or the process exits, which reads
exactly like the mod printing nothing at all. Launch it like this instead:

```
stdbuf -oL -eL ./BanjoRecompiled > game.log 2>&1
```

## Layout

```
mod.toml            manifest: config options, native library declaration
Makefile            MIPS build
mod.ld              MIPS linker script
build.sh            builds both halves, packages, optionally deploys
include/            recomp API headers, plus the shared MIPS/native contract
src/                MIPS mod code: main.c, overlay.c, speak.c, bootmap.c
native/             host library: irc_client, redemption_client, recomp_abi.h
helper/             the channel point redemption bridge
```

`native/recomp_abi.h` is a hand-written copy of the parts of the runtime's ABI
this library needs: the `recomp_context` layout, the rdram address translation
with its byteswap, and argument and return helpers. librecomp is not distributed
as an SDK, so there is nothing to include. The definitions came from
`N64Recomp/include/recomp.h` and `librecomp/include/librecomp/helpers.hpp`.

## Thunderstore

Thunderstore is where Banjo-Kazooie: Recompiled players actually look for mods,
and its mod manager installs them, which saves explaining mods folders to anyone.

```
./release.sh --thunderstore
```

builds one package covering both platforms. That works because the runtime picks
its native library by platform extension, so shipping `banjo_chatooie_native.so` and
`banjo_chatooie_native.dll` side by side lets each machine take the one it needs.

Thunderstore packages are laid out flat at the archive root, with `manifest.json`,
`icon.png` (exactly 256x256) and `README.md` required. The script checks the name
pattern, the version shape, the description length and the icon size before
zipping, because all of those are otherwise only rejected once you upload.

Upload at https://thunderstore.io/c/banjo-recompiled/create/. That part needs an
account and a team, and has to be done by hand the first time.
