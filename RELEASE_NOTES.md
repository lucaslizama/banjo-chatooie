**Beta.** Every character now draws the face you asked for, and the crash that
made 0.2.0 hard to recommend has been found.

Chat appears in a panel in the corner, and characters from the game can read
messages aloud in Banjo-Kazooie's own text boxes, with the right portrait and the
right voice.

    !say mumbo: hello banjo

## The crash is fixed

It was never in the mod. The build script installed the mod by overwriting the
files in place while the game had them open, which rewrote a library underneath a
running process. The game then called into an address that no longer held
anything and died, usually within about ten minutes of a rebuild.

That only ever happened to people building from source. Installing a release, or
using the Thunderstore mod manager, never touches a file the game is holding
open, so if that is how you installed 0.2.0 you were not hitting this. If you did
build it yourself, that is what those crashes were.

## Every character now draws who you asked for

The portrait numbers came from a community-maintained list that turned out to be
one entry short, so some names drew their neighbour instead. They are now read
from the game's own portrait table.

Three were plainly wrong. Asking for **Klungo** got you a gift-wrapped Christmas
present. **Lockup** drew Grunty, **Vile** drew Lockup, and **Tooty** drew her
post-transformation self rather than her usual one.

There are also **23 more characters** to talk to, which were simply missing:
Croc-Banjo, Sexy Grunty, young Eyrie, Little Lockup, the Tiptup choir member,
Snorkel, the polar bear cubs, the Click Clock Wood insects, the Freezeezy Peak
presents and the warp cauldron. Seventy-four names in all, listed in the README.

## Blubber can interrupt a chat box again

He was the only character in the game who could not, because of the way the mod
borrows one of his dialogue lines to carry its own text. Walking up to him now
cancels the chat box immediately, and the message that was displaced is spoken
afterwards rather than lost.

The same borrowing could, under the wrong timing, have him read out a chat message
instead of his own first-meeting line. That is fixed too, but honestly: it has not
been reproduced in play, because triggering it needs a save that has not met him
yet. If you ever see Blubber say something a viewer typed, please report it.

## Also

A buffer large enough to overflow Banjo-Kazooie's main thread stack was being
built on that stack every time the channel setting changed. The game's whole main
loop has 6128 bytes and this wanted 3328 of them. Anything could have come of
that; nothing good.

Messages queues no longer allocate memory while the game is running.

## Not a bug, for the record

You cannot talk to Brentilda while a chat box is on screen. That is how the
original game behaves -- it drops her request whenever any text box is up, and
you can see the same thing during one of Grunty's own random taunts with no mods
loaded at all. A chat box is simply one more thing that can be in the way.

## Still untested

Nobody has run this on a live stream, and the channel points feature has only
talked to Twitch's mock server, so signing in and creating the reward for real
are unproven. Please say what goes wrong.
