#ifndef TWITCH_SPEAK_H
#define TWITCH_SPEAK_H

#include "twitch_chat_abi.h"

// Queues a message to be spoken through the game's own dialogue box. The
// character is taken from a `name:` prefix on the message when there is one,
// otherwise `default_portrait` is used.
//
// `show_name` prefixes the chatter's name to the spoken text.
// `from_chat` marks a message that came from the channel's chat, as opposed to a
// channel point redemption. Only chat messages are dropped when the channel
// changes; a redemption was never tied to a channel in the first place.
void speak_queue(const char* user, const char* text, int default_portrait,
                 int show_name, int from_chat);

// Called once a frame. Starts the next queued message when the game is in a
// state that can show a dialogue box and no other dialogue is up.
void speak_tick(void);

// Drops queued messages that came from chat, used when the channel changes so the
// channel you just left does not keep talking. Redemptions are kept, since they
// were never tied to a channel. A box already on screen is not retracted; the
// game owns that one.
void speak_clear_chat(void);

// Looks up a character name (case-insensitive) in the portrait table.
// Returns the GcZoomboxSprite id, or -1 if the name isn't a known character.
int speak_lookup_portrait(const char* name);

// Portrait used when a message has no recognisable `name:` prefix.
#define SPEAK_DEFAULT_PORTRAIT 0x0F /* ZOOMBOX_SPRITE_F_BOTTLES */

#endif // TWITCH_SPEAK_H
