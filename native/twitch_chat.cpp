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
#include "redemption_client.h"

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

// void twitch_redemptions_start(int port)
//
// Connects to the helper process on loopback. Retries quietly if it isn't
// running, so the mod works with or without it.
RECOMP_EXPORT void twitch_redemptions_start(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    twitch::redemptions().start((int)arg_u32(ctx, 0));
}

// void twitch_redemptions_set_reward_title(const char* title)
//
// Names the reward the helper should watch. Called from the settings poll, so it
// is given the same value repeatedly and only acts on changes.
RECOMP_EXPORT void twitch_redemptions_set_reward_title(uint8_t* rdram, recomp_context* ctx) {
    std::string title = arg_string(rdram, ctx, 0, 160);
    twitch::redemptions().set_reward_title(title.c_str());
}

// void twitch_redemptions_stop(void)
RECOMP_EXPORT void twitch_redemptions_stop(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    (void)ctx;
    twitch::redemptions().stop();
}

// int twitch_redemptions_get_state(void)
RECOMP_EXPORT void twitch_redemptions_get_state(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    ret_s32(ctx, (int32_t)twitch::redemptions().state());
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

// int twitch_chat_next_message(char* user_out, char* text_out, int* color_out,
//                              int* flags_out)
//
// Pops one message into the caller's buffers, which must be
// TWITCH_USER_CAPACITY and TWITCH_TEXT_CAPACITY bytes. `flags_out` receives a
// bitmask of TWITCH_MSG_*. Returns 1 if a message was written and 0 if the
// queue is empty.
//
// Four arguments is the ceiling here: a fifth would come off the guest stack
// rather than a register.
RECOMP_EXPORT void twitch_chat_next_message(uint8_t* rdram, recomp_context* ctx) {
    uint32_t user_addr = arg_u32(ctx, 0);
    uint32_t text_addr = arg_u32(ctx, 1);
    uint32_t color_addr = arg_u32(ctx, 2);
    uint32_t flags_addr = arg_u32(ctx, 3);

    // Redemptions first: someone spent points on theirs, so it should not sit
    // behind a backlog of ordinary chat.
    twitch::ChatMessage msg;
    if (!twitch::redemptions().pop(msg) && !twitch::client().pop(msg)) {
        ret_s32(ctx, 0);
        return;
    }

    write_string(rdram, user_addr, msg.user, TWITCH_USER_CAPACITY);
    write_string(rdram, text_addr, msg.text, TWITCH_TEXT_CAPACITY);
    if (color_addr != 0) {
        mem_write_u32(rdram, color_addr, (uint32_t)msg.color);
    }
    if (flags_addr != 0) {
        uint32_t flags = 0;
        if (msg.highlighted) {
            flags |= TWITCH_MSG_HIGHLIGHTED;
        }
        if (msg.privileged) {
            flags |= TWITCH_MSG_PRIVILEGED;
        }
        if (msg.redeemed) {
            flags |= TWITCH_MSG_REDEEMED;
        }
        mem_write_u32(rdram, flags_addr, flags);
    }

    ret_s32(ctx, 1);
}
