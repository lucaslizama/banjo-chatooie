// Mod entry points.
//
// Two hooks drive everything:
//   recomp_on_init  -- build the overlay and open the connection
//   mainLoop        -- once per frame: drain new chat, follow config changes
//
// mainLoop is Banjo-Kazooie's per-frame function (bk-decomp src/core1/code_0.c),
// called from mainThread_entry's loop. The recomp runtime only declares a single
// event of its own, recomp_on_init, so a base-game hook is how a mod gets a tick.

#include "modding.h"
#include "recompconfig.h"
#include "recomputils.h"

#include "twitch_chat.h"
#include "overlay.h"

// How many messages to move into the overlay per frame. A busy channel can
// deliver faster than this, but draining the whole queue in one frame would let
// chat dictate frame time.
#define MESSAGES_PER_FRAME 3

// Config is only re-read periodically -- reading a string option allocates, and
// none of these settings need frame-accurate response.
#define SETTINGS_POLL_FRAMES 30
#define CHANNEL_POLL_FRAMES 120

static char sUserBuffer[TWITCH_USER_CAPACITY];
static char sTextBuffer[TWITCH_TEXT_CAPACITY];
static char sChannel[64];

static int sFrame = 0;
static int sLastState = -1;
static int sHideCommands = 1;
static int sOverlayReady = 0;

static int str_equal(const char* a, const char* b) {
    while (*a != '\0' && *b != '\0') {
        if (*a != *b) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == *b;
}

static void str_copy(char* dst, const char* src, int capacity) {
    int i = 0;
    if (capacity <= 0) {
        return;
    }
    while (src[i] != '\0' && i < capacity - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void read_settings(OverlaySettings* out) {
    out->line_count = (unsigned long)recomp_get_config_double("line_count");
    out->corner = recomp_get_config_u32("corner");
    out->text_size = (float)recomp_get_config_double("text_size");
    out->background_opacity = (float)recomp_get_config_double("background_opacity") / 100.0f;
    sHideCommands = recomp_get_config_u32("hide_commands") != 0;
}

// Returns 1 when the configured channel changed (including the first read).
static int refresh_channel(void) {
    char* configured = recomp_get_config_string("channel");
    int changed = 0;

    if (configured == NULL) {
        return 0;
    }

    if (!str_equal(configured, sChannel)) {
        str_copy(sChannel, configured, (int)sizeof(sChannel));
        changed = 1;
    }

    recomp_free_config_string(configured);
    return changed;
}

static void str_append(char* dst, const char* src, int capacity) {
    int len = 0;
    while (dst[len] != '\0' && len < capacity - 1) {
        len++;
    }
    str_copy(dst + len, src, capacity - len);
}

static void report_state(int state) {
    static char status[96];

    if (state == sLastState) {
        return;
    }
    sLastState = state;

    switch (state) {
        case TWITCH_STATE_CONNECTED:
            // The first message replaces this, so it only shows while chat is quiet.
            str_copy(status, "Connected to #", (int)sizeof(status));
            str_append(status, sChannel, (int)sizeof(status));
            overlay_set_status(status);
            break;
        case TWITCH_STATE_CONNECTING:
            overlay_set_status("Connecting to Twitch chat...");
            break;
        case TWITCH_STATE_ERROR:
            overlay_set_status("Twitch chat: connection failed, retrying...");
            break;
        case TWITCH_STATE_IDLE:
        default:
            overlay_set_status("Twitch chat: set a channel in the mod options.");
            break;
    }
}

RECOMP_CALLBACK("*", recomp_on_init) void twitch_chat_init(void) {
    // Only the connection is started here. The overlay is built on the first
    // frame instead, so the UI system is definitely up before we touch it.
    refresh_channel();
    if (sChannel[0] != '\0') {
        twitch_chat_start(sChannel);
    }
}

RECOMP_HOOK("mainLoop") void twitch_chat_tick(void) {
    int drained;
    int color;

    if (!sOverlayReady) {
        OverlaySettings settings;
        read_settings(&settings);
        overlay_create(&settings);
        overlay_show();
        sOverlayReady = 1;
    }

    sFrame++;

    if ((sFrame % SETTINGS_POLL_FRAMES) == 0) {
        OverlaySettings settings;
        read_settings(&settings);
        overlay_apply_settings(&settings);
    }

    if ((sFrame % CHANNEL_POLL_FRAMES) == 0 && refresh_channel()) {
        if (sChannel[0] != '\0') {
            recomp_printf("[twitch-chat] switching to channel %s\n", sChannel);
            twitch_chat_start(sChannel);
        } else {
            twitch_chat_stop();
        }
        sLastState = -1;
    }

    report_state(twitch_chat_get_state());

    for (drained = 0; drained < MESSAGES_PER_FRAME; drained++) {
        if (!twitch_chat_next_message(sUserBuffer, sTextBuffer, &color)) {
            break;
        }
        if (sHideCommands && sTextBuffer[0] == '!') {
            continue;
        }
        overlay_push_line(sUserBuffer, sTextBuffer, color);
    }
}
