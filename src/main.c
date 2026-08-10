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
static int sOverlayEnabled = 1;
// A freshly created context is not shown, so this starts at 0 rather than at a
// "don't know yet" sentinel. Getting that wrong meant the first apply could try
// to hide a context that had never been shown, which the UI system reports as
// "Attemped to hide a context that isn't shown".
static int sOverlayVisible = 0;

// Values of the "speak_trigger" config option. These must stay in the same order
// as the `options` list in mod.toml -- the runtime returns the chosen index.
#define SPEAK_TRIGGER_OFF        0
#define SPEAK_TRIGGER_REDEEMED   1
#define SPEAK_TRIGGER_HIGHLIGHT  2
#define SPEAK_TRIGGER_COMMAND    3
#define SPEAK_TRIGGER_EVERYTHING 4

// Loopback port the helper serves redemptions on. Matches the helper's default.
#define REDEMPTION_PORT 47474

#define SPEAK_PERMISSION_ANYONE 0

// The chat command that makes a character speak, e.g. "!say mumbo: hello".
#define SPEAK_COMMAND "!say"
#define SPEAK_COMMAND_LEN 4

static int sSpeakTrigger = SPEAK_TRIGGER_COMMAND;
static int sSpeakPermission = SPEAK_PERMISSION_ANYONE;
static int sSpeakShowName = 1;
static int sDefaultPortrait = SPEAK_DEFAULT_PORTRAIT;

// What the overlay was last told, so a poll that changes nothing costs nothing.
static OverlaySettings sLastSettings;

static void read_settings(OverlaySettings* out) {
    out->line_count = (unsigned long)recomp_get_config_double("line_count");
    out->corner = recomp_get_config_u32("corner");
    out->text_size = (float)recomp_get_config_double("text_size");
    out->background_opacity = (float)recomp_get_config_double("background_opacity") / 100.0f;
    sHideCommands = recomp_get_config_u32("hide_commands") != 0;
    sSpeakTrigger = (int)recomp_get_config_u32("speak_trigger");
    sSpeakPermission = (int)recomp_get_config_u32("speak_permission");
    sSpeakShowName = recomp_get_config_u32("speak_show_name") != 0;
    sOverlayEnabled = recomp_get_config_u32("overlay_enabled") != 0;
}

// Shows or hides the corner panel to match the config. Hiding it doesn't stop
// the mod reading chat -- messages still arrive and can still be spoken by a
// character, there is just nothing drawn in the corner.
static void apply_overlay_visibility(void) {
    if (sOverlayEnabled == sOverlayVisible) {
        return;
    }
    sOverlayVisible = sOverlayEnabled;

    if (sOverlayVisible) {
        overlay_show();
    } else {
        overlay_hide();
    }
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

// Returns 1 when the configured channel changed and has settled.
//
// The setting is a text field being typed into, and this runs every couple of
// seconds, so a partial name like "vi" on the way to "vixen" would otherwise
// trigger a full disconnect and reconnect to Twitch. Waiting for the same value
// twice in a row means only what you actually finished typing is acted on.
static int refresh_channel(void) {
    static char sPendingChannel[64];
    char* configured = recomp_get_config_string("channel");
    char* start;
    int end;
    int changed = 0;

    if (configured == NULL) {
        return 0;
    }

    // Trim surrounding whitespace. A stray space is easy to type and would
    // otherwise read as a different channel from the same name without it, and
    // a field holding only spaces would look set rather than empty.
    start = configured;
    while (*start == ' ' || *start == '\t') {
        start++;
    }
    end = twitch_strlen(start);
    while (end > 0 && (start[end - 1] == ' ' || start[end - 1] == '\t')) {
        end--;
    }
    start[end] = '\0';

    if (!twitch_streq(start, sChannel)) {
        if (twitch_streq(start, sPendingChannel)) {
            twitch_strcpy(sChannel, start, (int)sizeof(sChannel));
            changed = 1;
        } else {
            twitch_strcpy(sPendingChannel, start, (int)sizeof(sPendingChannel));
        }
    }

    recomp_free_config_string(configured);
    return changed;
}

// Decides whether a message should be spoken in game, and returns the part of it
// to speak (which strips the "!say" for the command trigger). NULL means don't.
static const char* speakable_body(const char* text, int flags) {
    int i;

    switch (sSpeakTrigger) {
        case SPEAK_TRIGGER_REDEEMED:
            return (flags & TWITCH_MSG_REDEEMED) ? text : NULL;

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
        apply_overlay_visibility();
        sOverlayReady = 1;
        sLastSettings = settings;
    }

    sFrame++;

    if ((sFrame % SETTINGS_POLL_FRAMES) == 0) {
        OverlaySettings settings;
        read_settings(&settings);
        read_default_portrait();
        apply_overlay_visibility();

        // Only run the redemption reader when it's the chosen trigger -- it
        // means a helper process, so there is no point connecting otherwise.
        if (sSpeakTrigger == SPEAK_TRIGGER_REDEEMED) {
            twitch_redemptions_start(REDEMPTION_PORT);
        } else {
            twitch_redemptions_stop();
        }

        // Only touch the UI when something actually changed. Re-applying every
        // poll meant ~30 recompui mutations twice a second forever, which is a
        // lot of churn to ask of the UI system for no visible effect.
        if (settings.line_count != sLastSettings.line_count ||
            settings.corner != sLastSettings.corner ||
            settings.text_size != sLastSettings.text_size ||
            settings.background_opacity != sLastSettings.background_opacity) {
            overlay_apply_settings(&settings);
            sLastSettings = settings;
        }
    }

    if ((sFrame % CHANNEL_POLL_FRAMES) == 0 && refresh_channel()) {
        if (sChannel[0] != '\0') {
            recomp_printf("[twitch-chat] switching to channel %s\n", sChannel);
            twitch_chat_start(sChannel);
        } else {
            twitch_chat_stop();
        }
        // The native side drops its own queue on a channel change; this drops
        // ours, so characters don't carry on reading out the channel you left.
        // The overlay clears itself, because the connection state change below
        // replaces its contents with a status line.
        speak_clear();
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
