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
#include "twitch_str.h"
#include "overlay.h"
#include "speak.h"

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
// Values of the "speak_trigger" config option, in menu order.
#define SPEAK_TRIGGER_OFF        0
#define SPEAK_TRIGGER_HIGHLIGHT  1
#define SPEAK_TRIGGER_COMMAND    2
#define SPEAK_TRIGGER_EVERYTHING 3

#define SPEAK_PERMISSION_ANYONE 0

// The chat command that makes a character speak, e.g. "!say mumbo: hello".
#define SPEAK_COMMAND "!say"
#define SPEAK_COMMAND_LEN 4

static int sSpeakTrigger = SPEAK_TRIGGER_COMMAND;
static int sSpeakPermission = SPEAK_PERMISSION_ANYONE;
static int sSpeakShowName = 1;
static int sDefaultPortrait = SPEAK_DEFAULT_PORTRAIT;

static void read_settings(OverlaySettings* out) {
    out->line_count = (unsigned long)recomp_get_config_double("line_count");
    out->corner = recomp_get_config_u32("corner");
    out->text_size = (float)recomp_get_config_double("text_size");
    out->background_opacity = (float)recomp_get_config_double("background_opacity") / 100.0f;
    sHideCommands = recomp_get_config_u32("hide_commands") != 0;
    sSpeakTrigger = (int)recomp_get_config_u32("speak_trigger");
    sSpeakPermission = (int)recomp_get_config_u32("speak_permission");
    sSpeakShowName = recomp_get_config_u32("speak_show_name") != 0;
}

// The default character is a name typed into a config field rather than an enum,
// because the portrait list is 50 entries long and an enum that size is unusable
// in the mod menu. An unrecognised name falls back to Bottles.
static void read_default_portrait(void) {
    char* configured = recomp_get_config_string("default_character");
    int portrait;

    if (configured == NULL) {
        return;
    }

    portrait = speak_lookup_portrait(configured);
    sDefaultPortrait = (portrait >= 0) ? portrait : SPEAK_DEFAULT_PORTRAIT;

    recomp_free_config_string(configured);
}

// Returns 1 when the configured channel changed (including the first read).
static int refresh_channel(void) {
    char* configured = recomp_get_config_string("channel");
    int changed = 0;

    if (configured == NULL) {
        return 0;
    }

    if (!twitch_streq(configured, sChannel)) {
        twitch_strcpy(sChannel, configured, (int)sizeof(sChannel));
        changed = 1;
    }

    recomp_free_config_string(configured);
    return changed;
}

// Decides whether a message should be spoken in game, and returns the part of it
// to speak (which strips the "!say" for the command trigger). NULL means don't.
static const char* speakable_body(const char* text, int flags) {
    int i;

    switch (sSpeakTrigger) {
        case SPEAK_TRIGGER_HIGHLIGHT:
            return (flags & TWITCH_MSG_HIGHLIGHTED) ? text : NULL;

        case SPEAK_TRIGGER_EVERYTHING:
            return text;

        case SPEAK_TRIGGER_COMMAND:
            if (sSpeakPermission != SPEAK_PERMISSION_ANYONE &&
                !(flags & TWITCH_MSG_PRIVILEGED)) {
                return NULL;
            }
            for (i = 0; i < SPEAK_COMMAND_LEN; i++) {
                if (twitch_lower(text[i]) != SPEAK_COMMAND[i]) {
                    return NULL;
                }
            }
            // Require a separator so "!saying hello" isn't treated as a command.
            if (text[SPEAK_COMMAND_LEN] != ' ') {
                return NULL;
            }
            text += SPEAK_COMMAND_LEN;
            while (*text == ' ') {
                text++;
            }
            return (*text != '\0') ? text : NULL;

        case SPEAK_TRIGGER_OFF:
        default:
            return NULL;
    }
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
            twitch_strcpy(status, "Connected to #", (int)sizeof(status));
            twitch_strcat(status, sChannel, (int)sizeof(status));
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
    int flags;

    if (!sOverlayReady) {
        OverlaySettings settings;
        read_settings(&settings);
        read_default_portrait();
        overlay_create(&settings);
        overlay_show();
        sOverlayReady = 1;
    }

    sFrame++;

    if ((sFrame % SETTINGS_POLL_FRAMES) == 0) {
        OverlaySettings settings;
        read_settings(&settings);
        overlay_apply_settings(&settings);
        read_default_portrait();
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
        const char* spoken;

        if (!twitch_chat_next_message(sUserBuffer, sTextBuffer, &color, &flags)) {
            break;
        }

        // Decided before the bot-command filter, so "!say" still works with
        // "Hide Bot Commands" on.
        spoken = speakable_body(sTextBuffer, flags);

        if (!(sHideCommands && sTextBuffer[0] == '!')) {
            overlay_push_line(sUserBuffer, sTextBuffer, color);
        }

        if (spoken != NULL) {
            speak_queue(sUserBuffer, spoken, sDefaultPortrait, sSpeakShowName);
        }
    }

    speak_tick();
}
