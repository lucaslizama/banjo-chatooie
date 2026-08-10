**Beta.** Much more functional than 0.1.0, and one known crash remains. Read the
last two sections before installing.

Chat appears in a panel in the corner, and characters from the game can read
messages aloud in Banjo-Kazooie's own text boxes, with the right portrait and the
right voice.

    !say mumbo: hello banjo

## New since 0.1.0

**Story dialogue now wins.** An NPC or a cutscene closes a chat box that is in
the way, instead of waiting behind it, and the interrupted message is spoken
afterwards rather than lost.

**Chat can no longer take the game down.** Several inputs used to crash or hang
it outright, all of which are now handled:

- accented letters, which crashed the game the moment anyone typed one
- a word longer than the text box, which sent the game's own line-break scan off
  the front of the string
- emoji, Japanese and anything else the 1998 font cannot draw, which produced an
  empty box that wedged every later conversation
- text boxes left open across a map transition
- messages truncated mid-character, which handed the interface invalid UTF-8

**Channel point redemptions**, through a small helper script. Optional, and
described in `helper/README.md`.

**An overlay toggle**, so you can have the in-game text boxes without the corner
panel. Plus a queue that holds messages during cutscenes rather than dropping
them, and a channel field that waits until you have finished typing.

## Known crash

The game still crashes after roughly ten minutes with the mod fully active. It is
not diagnosed. It leaves a normal crash, so nothing is corrupted, but you will
lose unsaved progress.

An evening of bisection cleared the mod loaded but idle, the chat reader, and the
dialogue injection, each over fifteen minutes of play. The one configuration not
yet cleared is channel point redemptions, which is where the search resumes.

If you would rather avoid it entirely, `!say` and the overlay have both had long
clean runs.

## If you installed 0.1.0, do not use its Debug: Boot To Map option

In 0.1.0 that option skipped the file select, which left the game's save state
blank, and Banjo-Kazooie writes that state out by itself at checkpoints. It
overwrote a real save with an empty one, and the backup with it. A save was lost
that way during development.

From 0.2.0 the option redirects the game to a separate scratch save file and
cannot touch yours. Updating is the fix; the option is off by default either way.

## Installing

Unzip and copy **both** files into your mods folder, next to each other:

    banjo_chatooie.nrm
    banjo_chatooie_native.so     (or .dll on Windows)

Linux: `~/.config/BanjoRecompiled/mods`. Then enable the mod and put your channel
name into **Twitch Channel** in its options.

Linux and Windows builds are included. macOS is not: cross-compiling for it needs
the Xcode SDK, which Apple does not permit redistributing, so a build has to
happen on a Mac.

## About the connection

Anonymous and read-only. No account, no password, no token, and it cannot send
messages or join more than the one channel you name. Channel points are the
exception and ask for permission separately.
