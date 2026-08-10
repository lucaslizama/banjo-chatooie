**Beta.** Tested by two people on one machine and never on a live stream. The
channel points feature has only ever been tested against a stand-in, never a real
Twitch account. Bug reports are the point of this release.

Puts your Twitch chat into Banjo-Kazooie: Recompiled. Messages scroll in a panel
in the corner, and characters from the game can read them aloud in the game's own
text boxes, with the right portrait and the right voice.

Someone in chat types:

    !say mumbo: hello banjo

and Mumbo says it. Around fifty characters are available.

## Installing

Unzip and copy **both** files into your mods folder:

    banjo_chatooie.nrm
    banjo_chatooie_native.so

On Linux that is `~/.config/BanjoRecompiled/mods`. They must sit next to each
other: the mod will not load without the native library.

Then enable the mod in the game's mod menu and put your channel name into
**Twitch Channel** in its options. That is the only required setting.

## Platform support

This build is **Linux x86-64 only**.

The `.nrm` itself is portable, but the mod also needs a native library built for
your platform, and it will not load without one. Windows and macOS builds are not
included because there was no toolchain to cross-compile them. Building from
source on those platforms produces the same layout with `banjo_chatooie_native.dll` or
`banjo_chatooie_native.dylib` in place of the `.so`. See `DEVELOPING.md` in the repository.

## About the connection

The mod logs in to Twitch anonymously and read-only. It never asks for an
account, a password or a token, it cannot send messages, and it joins only the
one channel you name. Nothing is stored: messages live in memory and are gone
once they scroll away.

Channel point redemptions are the one exception. They are not part of Twitch chat
and need a separate authorisation and a small helper script, both optional and
both described in `helper/README.md`.

## Known limitations

The game's text boxes are from 1998 and are strict about what they will draw.
Everything appears in capitals, because the font has no lowercase letters.
Accented characters are folded to their plain equivalents, so "mañana" reads as
MANANA. Emoji, Japanese and similar are dropped, and a message made entirely of
those is skipped rather than opening an empty box.

A handful of character names may summon a neighbouring portrait instead of the
one you asked for. The game's portrait table has one more entry than the
community's naming of it, so some labels are off by one. Five names are
confirmed correct: banjo, kazooie, tooty, grunty and dingpot. If a name gives you
the wrong face, `!say #87: hello` picks portrait 87 directly.

Channel point features need Twitch Affiliate, since that is what channel points
require. The `!say` command works on any channel.
