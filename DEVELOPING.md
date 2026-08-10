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
library filename from the manifest name, so `banjo_chatooie_native` becomes
`banjo_chatooie_native.so` on Linux, `.dll` on Windows and `.dylib` on macOS, and
looks for it in the folder *containing* the `.nrm`. Putting it inside the `.nrm`
does nothing at all.

Channel point redemptions are a third piece, `helper/twitch_redemptions.py`,
described in `helper/README.md`. They are not carried over IRC, so they arrive
through a loopback socket instead.

## Building

```
./build.sh            # builds both halves and packages the .nrm
./build.sh --deploy   # also installs both files into ~/.config/BanjoRecompiled/mods
```

`--deploy` installs by writing a temp file and renaming it, never by overwriting
in place, and that is not a stylistic preference. `cp` opens the destination
O_TRUNC and rewrites the same inode, and the game mmaps the native library from
that inode. Overwriting it while the game is running rewrites the file under a
live mapping: clean pages get dropped and re-faulted from the new contents, and
pages past the temporary EOF fault outright. That produced a stream of
inexplicable segfaults with `rip = 0` at whatever indirect call the process
reached next, and cost about two sessions of debugging before the timestamps gave
it away -- the .so was rewritten at 13:51:49 and the game died at 13:51:50. A
rename swaps in a new inode instead, so a running game keeps the old one intact
until it exits.

Which also means a deploy mid-session is not the build you are testing. The
script says so when it notices the game running; restart it to pick up a build.

You need `clang` and `ld.lld` for the MIPS half, a host C++17 compiler for the
native half, and `tools/RecompModTool` from the
[N64Recomp releases page](https://github.com/N64Recomp/N64Recomp/releases). That
last one is gitignored, being a prebuilt binary.

The native library links libstdc++ and libgcc statically, so it does not depend on
the host's versions. The result has only libc and libm as external dependencies.

It is also built `-fno-plt` and linked `-Bsymbolic`, which leaves it with zero PLT
entries. **That was done for a reason that turned out to be wrong**, and the note
is kept so nobody repeats it. Core dumps kept showing calls from this library
landing on a PLT stub's unrelocated file offset, so the flags were added to remove
the indirection those calls went through. It changed nothing, because the real
cause was `cp` rewriting the library under a live mapping, which no link-time
option has any bearing on. The flags are harmless and can stay, but do not reach
for them to fix a crash, and do not read "zero PLT entries" as a safety property.

## Releasing

Bump `version` in `mod.toml`, then push a tag that matches it:

```
git tag v0.4.0 && git push origin v0.4.0
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

Release notes come from `RELEASE_NOTES.md`, so edit that before tagging.

The version must always go up. Thunderstore refuses a version number it has
already seen, and a GitHub tag would have to be moved, so any change after a
release means a new number rather than a rebuild of the old one. 0.1.0, 0.2.0 and
0.3.0 are published; the next one is 0.4.0.

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

`gcdialog_showDialogConditional` (dialog.c:898) has three outcomes, and they are
worth knowing exactly, because both NPC bugs below fall out of them:

- refused outright, `return 0`, if `VOLATILE_FLAG_1` or `func_802D686C()`
- no dialogue up: calls `func_80310B1C` **synchronously**, which reaches
  `loadAndCreateDialogs` -> `loadDialogStrings` -> `dialogBin_get`, and returns 1
- dialogue already up: queues into `g_Dialog.unk148[]` and returns 1 **only** if
  the caller passed `0x04` or `0x20` in arg1, otherwise `return 0` and the
  request is simply gone

The mod passes `arg1 = 0` and only calls this when `gcdialog_hasCurrentTextId()`
is false, so it always takes the synchronous branch. That makes a return of 1
equivalent to "`dialogBin_get` ran inside our call", which is what the
`sInjecting` guard depends on.

That third outcome explains Brentilda, and it is NOT a bug this mod introduced.
She passes neither flag, so while any dialogue is up her request hits `return 0`
and vanishes. Confirmed in play on 2026-08-10: she cannot be talked to during one
of Grunty's own random taunt boxes either, with or without this mod. Vanilla
behaves exactly the same way. Molehills and Leaky do pass a flag, which is why
they queue and recover by themselves.

So what the mod actually changes here is *frequency*, not correctness. A chat box
is one more thing that can be on screen when the player walks up to her. The
`sDeferred` capture-and-re-issue is therefore an improvement on vanilla rather
than a fix for a regression -- and note it has not yet been observed firing for
her specifically, so treat it as unproven for this case.

Borrowing a real id has a cost, and it is all Blubber's, because the carrier is
one of his lines. The id cannot distinguish our call from his, and that broke him
in two separate directions.

He could not interrupt a chat box, alone among every NPC in the game, because the
yield hook recognised our call by asset id and so mistook him for us. The hook
tests `sInjecting` instead now. Confirmed fixed in play on 2026-08-10: walking up
to him cancels a chat box immediately, and the displaced message stays queued and
is spoken after his conversation.

The other direction is a stale `sHijack` being served to him. Note the queue
replay at `dialog.c:819`, which calls `func_80310B1C` from the per-frame update:
it reaches `dialogBin_get` with `sInjecting` clear, and since our own call never
queues, whatever it replays belongs to somebody else. So the substitution requires
`sInjecting` as well as `sHijack`, and `sHijack` is cleared unconditionally once
our call returns rather than only when `dialogBin_get` consumed it. Without both,
he reads out a chat message instead of his own first-meeting dialogue.

That second half is NOT yet verified in play. It needs his first-meeting line,
asset `0xA0B`, which only fires on a save that has not met him -- so testing it on
a save where he is already known proves nothing about the leak.

## Engine landmines

Six things in the original game will softlock or crash it the moment you feed
arbitrary text through the dialogue system. None of them can be reached by Rare's
own dialogue, which is why they were never found. They share a root: this format
assumes data that shipped in the ROM, and validates none of it.

**Every entry list needs a terminator after the last text entry.** The `default:`
branch of `dialog_update` reads `CMD(string_index + 1)->cmd` for every non-empty
text entry, checking for the `-8` and `-9` conditional markers, so the game always
looks one entry past the one it is showing. Without an entry there it reads off
the end of the array `loadAndCreateDialogs` allocated, treats the garbage as the
next portrait and string, and leaves that box open forever. After that
`gcdialog_hasCurrentTextId()` never goes false again and no conversation anywhere
in the game can start.

**Each box's entry list needs several terminators, not one.** `DIALOG_STATE_6`,
which every closing dialogue passes through, scans forward from `string_index`
for a command between `-4` and `-1`:

```c
for (j = string_index[i]; dialog[i][j].cmd < -4 || dialog[i][j].cmd >= 0; j++)
```

No bound, and no reference to `string_count`. With a single close entry, a
`string_index` that has already moved past it sends that scan off the end of the
allocation to spin over whatever follows, which hangs the game rather than
crashing it. `build_blob` appends four. This is a different unbounded read from
the one above, reached from a different direction, and fixing one does not fix the
other.

**Never leave a box open across a map transition.** A transition tears down the
heap, and a box's entry arrays were allocated from it while `dialogBin_release`
frees the carrier asset back into it. Leaving one open across that is a dangling
pointer or an unbalanced free, and a corrupted allocator surfaces later as
unrelated code holding wrong addresses, which is a miserable thing to debug from
the far end. `speak_tick` closes ours as soon as `gctransition_active()` reports a
transition, and refuses to inject during one.

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
`0x91` up) and waits for 90 consecutive idle frames, three seconds, first.

One more, less dangerous but easy to trip over: `loadDialogStrings` copies the
portrait and length bytes into its own allocation, but keeps `str` as a pointer
straight into the blob for as long as the box is on screen. The blob is double
buffered because of it, or building the next message would rewrite live text
underneath the current one.

Long messages need no splitting, though. The zoombox wraps and scrolls by itself
at 24 characters per line.

## Portraits: read the array, not the enum

`kPortraits` is derived from `D_8036C6C0` in `bk-decomp/src/core2/gc/zoombox.c`.
That array is the authority. It holds 107 entries and every one names the sprite
asset it draws, so the correct index for any character can simply be read off it:

```c
gczoomboxPortraitInfo D_8036C6C0[] = {
     {ASSET_816_SPRITE_GRUNTILDA, 0xDA, 0xE5, {...
```

Do not use the `GcZoomboxSprite` enum instead. It names only 106, one short, and
its labels drift out of step with the real data. Three entries here were taken
from it and all three were wrong: `klungo` pointed at `0x5D`, which is a
Christmas present; `lockup` at `0x66`, which is Grunty; and `vile` at `0x67`,
which is Lockup. A viewer asking for Klungo got a gift-wrapped box.

Parsing the array beats probing by hand. Walking its braces and taking each
entry's first field yields the whole index-to-character map in one go, and doing
that turned a table of 51 entries with 3 errors into 74 with none.

Only `0x0C` and up is addressable, because the blob's cmd byte maps to `cmd +
0xC`. The twelve entries below it are unreachable, including the pretty Tooty at
`0x07` -- `0x42` is another copy of her that can be reached.

Spot-checked in play on 2026-08-10, all matching the array: `0x12` Conga, `0x41`
Grunty, `0x43` Boggy, `0x50` Nabnut, `0x55` Eyrie, `0x57` Brentilda, `0x5B`
Cheato, `0x61` Banjo, `0x62` Kazooie, `0x64` Dingpot -- and `0x5D` drawing a
present, which is what prompted reading the array in the first place.

`#<index>` addresses a portrait by raw index, which is how any of this gets
checked: `!say #87: test` and see who turns up. It also reaches the entries with
no name here, including the item icons at `0x25`-`0x30` and the three unnamed
sprites at `0x68`-`0x6A`. `helper/`-adjacent scratch scripts aside, the quickest
harness is a feed that sends `#<n>` probes with the number repeated in the body,
so the face and its index are on screen together.

## Booting straight into a map

`Debug: Boot To Map` skips the intro and file select. It works by patching
`getDefaultBootMap`, not by redirecting the boot afterwards, because `core1_init`
calls `setBootMap(getDefaultBootMap())` *and* `func_8023DA9C(3)` before
`mainThread_entry` ever enters its `while (1) mainLoop()` loop. Any per-frame
hook is far too late.

No save file is loaded when booting this way, so the world comes up as an empty
file, and the mod calls `recomp_change_save_file` to point the game at a scratch
save before it can touch anything.

That call is not optional. Skipping the file select leaves the game's in-memory
save state blank, and Banjo-Kazooie writes that state out by itself at
checkpoints such as entering the lair. Aimed at the normal save file it
overwrites a real save with a blank one, and then overwrites the backup with the
same. That happened during development and the save was unrecoverable.

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
