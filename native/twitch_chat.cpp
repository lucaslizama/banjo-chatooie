// Native library entry points, called by the game from the MIPS mod.
//
// The runtime finds this library by name: the manifest's
// `native_libraries = [{ name = "twitch_chat", ... }]` makes it look for
// `twitch_chat.so` (or .dll / .dylib) IN THE SAME FOLDER AS THE .nrm -- not
// inside the .nrm. build.sh deploys both together.
//
// Every function here runs on the game thread, so none of them may block. All
// the waiting happens on the IRC client's own thread.

#include "recomp_abi.h"
#include "irc_client.h"

#include "../include/twitch_chat_abi.h"

// Checked by the runtime before any function is looked up. Version 1 is the only
// one librecomp currently accepts.
RECOMP_EXPORT_DATA
uint32_t recomp_api_version = 1;
RECOMP_EXPORT_DATA_END

// void twitch_chat_start(const char* channel)
RECOMP_EXPORT void twitch_chat_start(uint8_t* rdram, recomp_context* ctx) {
    std::string channel = arg_string(rdram, ctx, 0, 64);
    twitch::client().start(channel);
}

// void twitch_chat_stop(void)
RECOMP_EXPORT void twitch_chat_stop(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    (void)ctx;
    twitch::client().stop();
}

// int twitch_chat_get_state(void)
RECOMP_EXPORT void twitch_chat_get_state(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    ret_s32(ctx, (int32_t)twitch::client().state());
}

// int twitch_chat_next_message(char* user_out, char* text_out, int* color_out)
//
// Pops one message into the caller's buffers, which must be
// TWITCH_USER_CAPACITY and TWITCH_TEXT_CAPACITY bytes. Returns 1 if a message
// was written and 0 if the queue is empty.
RECOMP_EXPORT void twitch_chat_next_message(uint8_t* rdram, recomp_context* ctx) {
    uint32_t user_addr = arg_u32(ctx, 0);
    uint32_t text_addr = arg_u32(ctx, 1);
    uint32_t color_addr = arg_u32(ctx, 2);

    twitch::ChatMessage msg;
    if (!twitch::client().pop(msg)) {
        ret_s32(ctx, 0);
        return;
    }

    write_string(rdram, user_addr, msg.user, TWITCH_USER_CAPACITY);
    write_string(rdram, text_addr, msg.text, TWITCH_TEXT_CAPACITY);
    if (color_addr != 0) {
        mem_write_u32(rdram, color_addr, (uint32_t)msg.color);
    }

    ret_s32(ctx, 1);
}
