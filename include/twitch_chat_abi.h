// Shared between the MIPS mod code and the native library.
//
// The buffer sizes below are part of the calling contract: twitch_chat_next_message
// writes into caller-provided buffers of exactly these sizes, so both sides must
// agree. That keeps the function at three arguments -- arguments past a0-a3 come
// off the stack, which is fiddlier to get right on the native side than it is
// worth here.

#ifndef TWITCH_CHAT_ABI_H
#define TWITCH_CHAT_ABI_H

#define TWITCH_USER_CAPACITY 40
#define TWITCH_TEXT_CAPACITY 256

// Connection state reported by twitch_chat_get_state.
#define TWITCH_STATE_IDLE       0
#define TWITCH_STATE_CONNECTING 1
#define TWITCH_STATE_CONNECTED  2
#define TWITCH_STATE_ERROR      3

// Returned in place of a colour when the chatter has never chosen one.
#define TWITCH_COLOR_NONE (-1)

#endif // TWITCH_CHAT_ABI_H
