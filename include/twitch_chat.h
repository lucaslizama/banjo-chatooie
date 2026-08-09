// Functions provided by this mod's native library (twitch_chat.so / .dll / .dylib).
//
// "." as the import module name means "this mod's own exports", which is how a
// mod reaches into a native library it ships with. The runtime resolves each of
// these against the `funcs` list in mod.toml's native_libraries entry, so a name
// added here must be added there too or the mod fails to load.

#ifndef TWITCH_CHAT_H
#define TWITCH_CHAT_H

#include "modding.h"
#include "twitch_chat_abi.h"

// Connects to the given channel, reconnecting if a different one is already
// active. An empty channel disconnects. Returns immediately; the connection is
// made on the library's own thread.
RECOMP_IMPORT(".", void twitch_chat_start(const char* channel));

// Disconnects and clears any queued messages.
RECOMP_IMPORT(".", void twitch_chat_stop());

// One of the TWITCH_STATE_* values.
RECOMP_IMPORT(".", int twitch_chat_get_state());

// Pops the oldest queued message. `user_out` must have room for
// TWITCH_USER_CAPACITY bytes and `text_out` for TWITCH_TEXT_CAPACITY; both come
// back zero-terminated. `color_out` receives 0xRRGGBB or TWITCH_COLOR_NONE, and
// `flags_out` a mask of TWITCH_MSG_*.
// Returns 1 if a message was written, 0 if there was nothing waiting.
RECOMP_IMPORT(".", int twitch_chat_next_message(char* user_out, char* text_out, int* color_out, int* flags_out));

// Channel point redemptions arrive from a helper process on loopback rather than
// over IRC, because custom rewards only exist behind the authenticated Helix
// API. Starting this when the helper isn't running is harmless -- it retries
// quietly and everything else carries on. Messages it produces come back from
// twitch_chat_next_message with TWITCH_MSG_REDEEMED set.
RECOMP_IMPORT(".", void twitch_redemptions_start(int port));
RECOMP_IMPORT(".", void twitch_redemptions_stop());
RECOMP_IMPORT(".", int twitch_redemptions_get_state());

#endif // TWITCH_CHAT_H
