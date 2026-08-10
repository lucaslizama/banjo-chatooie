# Banjo-Chatooie

Puts your Twitch chat into Banjo-Kazooie: Recompiled.

Messages scroll in a small panel in the corner of the screen. If you want, a
character from the game also reads them aloud in one of Banjo-Kazooie's own text
boxes, with that character's portrait and their voice — Bottles, Mumbo, Grunty,
Klungo, the Jinjos. Chat picks who speaks.

The connection is anonymous and read-only. The mod never asks for a Twitch
account, a password or a token, and it cannot send messages or join more than the
one channel you name. (Channel points are optional and ask for permission
separately.)

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
no `#`. That is the only required setting. Chat appears within a couple of
seconds, and you can change the channel while playing without restarting.

The panel shows each chatter's name in their own Twitch colour. You can move it to
any corner and change its width, how many lines it keeps, its text size and how
solid its background is. **Show Chat Overlay** turns it off completely, which is
what you want if you would rather chat only appeared through the game's text
boxes.

## Making characters talk

**Characters Speak On** decides what makes a character read a message out loud:

`!say command` is the default and works on any channel. Someone types
`!say hello everyone` in chat and a character says it. **Who Can Use !say** can
narrow that to you and your moderators if chat gets carried away.

`Channel point reward` uses a proper channel points redemption. It needs Twitch
Affiliate and a small script running alongside the game, described in
`helper/README.md`. **Reward Name** sets what the reward is called on your channel,
and changing it there renames it on Twitch while you play.

`Highlight My Message` uses the built-in reward of that name. Also Affiliate only.

`Every message` makes every single line of chat get spoken. Chaotic, but it is the
fastest way to see whether everything works.

### When the game wants to talk

Story dialogue always wins. Walk up to a molehill or an NPC while a chat box is on
screen and the chat box gets out of the way, so you never have to wait for chat to
finish before the game can talk to you. The interrupted message is spoken
afterwards rather than thrown away.

Nothing is spoken during a cutscene, or across a map transition. Messages that
arrive then wait their turn.

### Choosing who speaks

Start the message with a name and a colon. `!say mumbo: hello banjo` comes out of
Mumbo, in Mumbo's voice. Anything without a recognised name falls back to the
**Default Character** setting.

Every name chat can use, all case-insensitive:

**The main cast**
`banjo` · `kazooie` · `tooty` · `uglytooty` · `crocbanjo` · `bottles` · `mumbo` ·
`cheato`

**Grunty and her lot**
`grunty` · `gruntilda` · `sexygrunty` · `brentilda` · `klungo` · `dingpot` ·
`cauldron` · `warpcauldron` · `vile` · `lockup` · `littlelockup`

**Mumbo's Mountain**
`conga` · `chimpy` · `juju` · `ticker` · `termite`

**Treasure Trove Cove**
`blubber` · `nipper` · `snacker` · `yumyum` · `leaky`

**Clanker's Cavern**
`clanker` · `snippet` · `blacksnippet`

**Bubblegloop Swamp**
`tiptup` · `tanktup` · `flibbit` · `mummum` · `croctus` · `piranha` ·
`choirmember`

**Freezeezy Peak**
`boggy` · `wozza` · `twinkly` · `chomper` · `bearcubs` · `bluegift` ·
`greengift` · `redgift`

**Gobi's Valley**
`gobi` · `rubee` · `jinxy` · `ancientone` · `sandeel` · `grabba`

**Mad Monster Mansion**
`motzhand` · `tumblar` · `loggo` · `gloop` · `teehee`

**Rusty Bucket Bay**
`snorkel` · `boombox`

**Click Clock Wood**
`nabnut` · `eyrie` · `youngeyrie` · `gnawty` · `caterpillar` · `worm` · `zubba` ·
`trunker`

**Jinjos**
`jinjo` (yellow) · `yellowjinjo` · `greenjinjo` · `bluejinjo` · `pinkjinjo` ·
`orangejinjo`

You can also pick a portrait by number. `!say #87: hello` uses portrait 87
directly, and anything from 12 to 106 works. That reaches a handful of oddities
with no name of their own, including the collectable icons around 37 to 48.

## What chat cannot say

The game's text boxes were built in 1998 for text Rare wrote, so a few things do
not survive the trip.

Everything is drawn in capitals, because the font has no lowercase letters at all.
Accented letters become their plain equivalents, so "mañana" comes out as MANANA
rather than losing the letter. Emoji, Japanese and anything else without a
reasonable stand-in is dropped, and a message made entirely of those is skipped
instead of opening an empty box. Very long words get a space forced into them so
they fit the box.

None of this affects the corner panel, which shows messages exactly as typed,
emoji and all.

## Channel points

Redemptions are not part of Twitch chat, so they need a helper script running next
to the game and a one-time authorisation. Setup takes about two minutes and is
written up in `helper/README.md`.

It needs Twitch Affiliate. Without channel points on your account there is nothing
for it to read, and `!say` is the trigger to use instead.

## While you are testing

**Debug: Boot To Map** drops you straight into a level when the game starts,
skipping the intro and the file select. It loads no save file, so the world comes
up empty, and it switches the game to a separate scratch save file so your real
save is never touched. Useful for trying things quickly, not for playing. Leave it
off otherwise.

## Troubleshooting

If nothing appears in the corner, check the channel name is spelled right and has
no `#`. The panel says whether it is connecting, connected or failing.

If characters never speak, check **Characters Speak On** is not set to Off, and
remember that nothing is spoken during cutscenes. Messages that arrive then are
not lost — they wait and play once the cutscene ends.

If a character's portrait is not who you expected, `!say #<number>: test` will find
the right one.

Anything else, please open an issue on GitHub. Nothing the mod does touches your
save file.

## Building it yourself

See `DEVELOPING.md`.

## Licence

Creative Commons Attribution-ShareAlike 4.0 International (CC BY-SA 4.0).

Use it, change it, share it, build on it. Two conditions: credit the original, and
release anything you derive from it under the same licence, so it stays open for
the next person.

Banjo-Kazooie itself belongs to Nintendo and Rare. Nothing here contains any part
of the game, and you need your own copy to play it.
