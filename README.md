# Banjo-Chatooie

A mod for Banjo-Kazooie: Recompiled that puts your Twitch chat into the game.

**This is a beta.** It has been tested by two people on one machine, and never on
a live stream. The channel points feature works, and has been driven end to end
through Twitch's own mock API, but it has never talked to a live Twitch account,
so signing in and creating the reward for real are still unproven. It should not
break your game, and nothing it does touches your save file, but expect rough
edges and please say what goes wrong.

Two things happen with an incoming message. It scrolls in a small panel in the
corner of the screen, and, if you want, a character from the game reads it aloud
in one of Banjo-Kazooie's own text boxes, with that character's portrait and
their voice. Bottles, Mumbo, Grunty, Klungo, the Jinjos: chat picks who speaks.

The connection is anonymous and read-only. The mod never asks for a Twitch
account, a password or a token, and it cannot send messages or join more than the
one channel you name. (The optional channel points feature is the exception, and
it asks for permission separately. More on that below.)

## Installing

Copy both of these into your mods folder, which is
`~/.config/BanjoRecompiled/mods` on Linux:

- `banjo_chatooie.nrm`
- `banjo_chatooie_native.so`

They have to sit next to each other. The mod will not load if the second file is
missing, and putting it anywhere else has no effect.

Then enable the mod in the game's mod menu.

## Getting chat on screen

Open the mod's options and type your channel name into **Twitch Channel**, with
no `#`. That is the only required setting. Chat should appear within a couple of
seconds, and you can change the channel while playing without restarting.

The panel shows each chatter's name in their own Twitch colour. You can move it
to any corner, change how many lines it keeps, its text size and how solid its
background is. **Show Chat Overlay** turns it off completely, which is what you
want if you would rather chat only appeared through the game's text boxes.

## Making characters talk

**Characters Speak On** decides what makes a character read a message out loud:

`!say command` is the default and works on any channel. Someone types
`!say hello everyone` in chat and a character says it. **Who Can Use !say** can
narrow that to you and your moderators if chat gets carried away.

`Channel point reward` uses a proper channel points redemption. It needs Twitch
Affiliate and a small script running alongside the game, described in
`helper/README.md`.

`Highlight My Message` uses the built-in reward of that name. Also Affiliate only.

`Every message` makes every single line of chat get spoken. Chaotic, but it is
the fastest way to see whether everything works.

### Choosing who speaks

Start the message with a name and a colon. `!say mumbo: hello banjo` comes out of
Mumbo, in Mumbo's voice. Anything without a recognised name falls back to the
**Default Character** setting.

Every name chat can use, all case-insensitive:

| Main cast | Villains | Spiral Mountain and the lair | Worlds |
| --- | --- | --- | --- |
| banjo | grunty | bottles | gobi |
| kazooie | gruntilda | mumbo | rubee |
| tooty | klungo | brentilda | jinxy |
| dingpot | vile | cheato | croctus |
| | snacker | loggo | boggy |
| | lockup | eyrie | wozza |
| | grabba | nabnut | tiptup |
| | teehee | motzhand | tanktup |
| | twinkly | tumblar | trunker |
| | gnawty | mummum | clanker |
| | flibbit | juju | snippet |
| | zubba | yumyum | nipper |
| | | leaky | blubber |
| | | gloop | conga ✔ |
| | | | chimpy |

The Jinjos: `jinjo` (yellow), `yellowjinjo`, `greenjinjo`, `bluejinjo`,
`pinkjinjo`, `orangejinjo`.

Six of these are confirmed to draw who they say they do: banjo, kazooie, tooty,
grunty, dingpot and conga. The rest come from the community's names for the game's
portraits and a few are probably off by one, so you may get a neighbour instead
of the character you asked for. If you would rather pick by number,
`!say #87: hello` uses portrait 87 directly, and any number from 12 to 106 works.

## What chat cannot say

The game's text boxes were built in 1998 for text Rare wrote, so a few things do
not survive the trip:

Everything is drawn in capitals, because the font has no lowercase letters at
all. Accented letters are turned into their plain equivalents, so "mañana" comes
out as MANANA rather than losing the letter. Emoji, Japanese and anything else
without a reasonable stand-in is dropped, and a message made entirely of those is
skipped instead of opening an empty box. Very long words get a space forced into
them, because a word wider than the text box crashes the game.

None of this affects the corner panel, which shows messages exactly as typed,
emoji and all.

## Channel points

Redemptions are not part of Twitch chat, so they need a helper script running
next to the game and a one-time authorisation. Setup takes about two minutes and
is written up in `helper/README.md`.

It needs Twitch Affiliate. Without channel points on your account there is
nothing for it to read, and `!say` is the trigger to use instead.

## While you are testing

**Debug: Boot To Map** drops you straight into a level when the game starts,
skipping the intro and the file select. It loads no save file, so the world comes
up empty. Useful for trying things quickly, not for playing. Leave it off
otherwise.

## Troubleshooting

If nothing appears in the corner, check the channel name is spelled right and has
no `#`. The panel tells you what it is doing, so it will say whether it is
connecting, connected or failing.

If characters never speak, check **Characters Speak On** is not set to Off, and
remember that nothing is spoken during cutscenes. Messages that arrive then are
not lost, they wait and play once the cutscene ends.

If a character's portrait is not who you expected, that is the numbering problem
described above. Find the right one with `!say #<number>: test`.

## Building it yourself

See `DEVELOPING.md`.

## Licence

Creative Commons Attribution-ShareAlike 4.0 International (CC BY-SA 4.0).

Use it, change it, share it, build on it. Two conditions: credit the original,
and release anything you derive from it under the same licence, so it stays open
for the next person.

Banjo-Kazooie itself belongs to Nintendo and Rare. Nothing here contains any part
of the game, and you need your own copy to play it.
