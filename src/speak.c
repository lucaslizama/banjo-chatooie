// Renders chat messages through Banjo-Kazooie's own dialogue boxes.
//
// How this works
// --------------
// The game builds a dialogue from a byte blob returned by `dialogBin_get`:
//
//     [count][cmd][len][text...] x count      <- bottom box
//     [count][cmd][len][text...] x count      <- top box
//
// `cmd` selects the portrait (`gczoombox_new` is called with `cmd + 0xC`), and
// the portrait id also picks the character's voice samples -- `__gczoombox_load_sfx`
// reads both out of the same `D_8036C6C0[portrait_id]` table. So choosing a
// character gets the face and the voice together, for free.
//
// Rather than invent a text id, we hijack one real dialogue asset for exactly
// one call: `speak_tick` sets sHijack, calls gcdialog_showDialog with that
// asset, and our patched `dialogBin_get` returns our blob instead of the asset's
// text and clears the flag. The asset itself is still acquired and released
// normally, which matters because `dialogBin_release` frees whatever
// `s_dialogBin.ptr` points at -- an invented id would unbalance that.
//
// The zoombox wraps and scrolls long text by itself (`_gczoombox_findLineBreak`
// at 24 characters), so a whole chat message can go in as one string.

#include "speak.h"

#include "modding.h"
#include "recomputils.h"
#include "functions.h"
#include "enums.h"

#include "twitch_str.h"

// Any real dialogue asset works as the carrier. This one is Blubber's
// first-meeting line: it only ever plays in Treasure Trove Cove, and we only
// override it during the single call we make ourselves.
#define SPEAK_CARRIER_ASSET 0xA0B

// `len` in the blob is a single byte, so a string plus its terminator has to fit
// in 255. Chat text is already capped well below that.
#define SPEAK_MAX_TEXT 200

#define SPEAK_QUEUE_SIZE 6

typedef struct {
    char text[SPEAK_MAX_TEXT];
    int portrait;
} SpeakRequest;

static SpeakRequest sQueue[SPEAK_QUEUE_SIZE];
static int sQueueHead = 0;
static int sQueueCount = 0;

// Blob handed to the game in place of the carrier asset's text.
static unsigned char sBlob[4 + SPEAK_MAX_TEXT + 4];
static int sHijack = 0;

// Set once we've logged a real asset's first bytes, so the format check below
// only prints once per session.
static int sDumpedRealAsset = 0;

// From the base game. `s_dialogBin` is file-static in code_94620.c but the
// recomp symbol files expose it, so the patched `dialogBin_get` can keep it
// consistent exactly as the original did.
extern struct {
    unsigned char unk0;
    char* ptr;
    s32 index;
} s_dialogBin;
extern s32 code94620_func_8031B5B0(void);
extern s32 gcdialog_hasCurrentTextId(void);
extern s32 getGameMode(void);
extern u32 D_8027A130;

typedef struct {
    const char* name;
    int portrait;
} PortraitEntry;

// Only portraits 0x0C and up are reachable: the blob's cmd byte maps to
// `cmd + 0xC`, so anything below that can't be addressed. Every major speaking
// character is above the line.
static const PortraitEntry kPortraits[] = {
    { "banjo",      0x60 }, { "kazooie",    0x61 }, { "bottles",    0x0F },
    { "mumbo",      0x10 }, { "grunty",     0x41 }, { "gruntilda",  0x41 },
    { "tooty",      0x42 }, { "brentilda",  0x57 }, { "cheato",     0x5B },
    { "klungo",     0x5D }, { "boggy",      0x43 }, { "wozza",      0x44 },
    { "gobi",       0x1D }, { "rubee",      0x1C }, { "tiptup",     0x18 },
    { "tanktup",    0x19 }, { "trunker",    0x1B }, { "clanker",    0x15 },
    { "snacker",    0x3B }, { "chimpy",     0x11 }, { "conga",      0x12 },
    { "blubber",    0x13 }, { "nipper",     0x14 }, { "snippet",    0x16 },
    { "flibbit",    0x1A }, { "grabba",     0x1E }, { "teehee",     0x1F },
    { "juju",       0x35 }, { "yumyum",     0x36 }, { "leaky",      0x38 },
    { "gloop",      0x39 }, { "jinxy",      0x3F }, { "croctus",    0x40 },
    { "motzhand",   0x45 }, { "tumblar",    0x46 }, { "mummum",     0x47 },
    { "zubba",      0x4F }, { "gnawty",     0x4D }, { "twinkly",    0x4B },
    { "nabnut",     0x50 }, { "eyrie",      0x55 }, { "loggo",      0x5A },
    { "dingpot",    0x63 }, { "lockup",     0x66 }, { "vile",       0x67 },
    { "jinjo",      0x20 }, { "yellowjinjo", 0x20 }, { "greenjinjo", 0x21 },
    { "bluejinjo",  0x22 }, { "pinkjinjo",  0x23 }, { "orangejinjo", 0x24 },
};

#define PORTRAIT_COUNT ((int)(sizeof(kPortraits) / sizeof(kPortraits[0])))

int speak_lookup_portrait(const char* name) {
    int i;
    for (i = 0; i < PORTRAIT_COUNT; i++) {
        if (twitch_streq_lower(name, kPortraits[i].name)) {
            return kPortraits[i].portrait;
        }
    }
    return -1;
}

// Faithful reimplementation of the original, with our blob substituted for one
// call. A RECOMP_PATCH replaces the function outright, so the original body has
// to be reproduced rather than delegated to.
RECOMP_PATCH char* dialogBin_get(s32 text_id) {
    char* header;
    char* result;
    s32 offset;

    s_dialogBin.ptr = (char*)assetcache_get(text_id);
    header = s_dialogBin.ptr + 1 + code94620_func_8031B5B0() * 2;
    offset = (unsigned char)header[0];
    offset += ((unsigned char)header[1]) << 8;
    s_dialogBin.index = text_id;
    result = s_dialogBin.ptr + offset;

    // One-shot format check: the blob we synthesize has to agree with what the
    // real assets look like, in particular whether `len` counts the terminator.
    if (!sDumpedRealAsset && !sHijack) {
        int i;
        sDumpedRealAsset = 1;
        recomp_printf("[twitch-chat] dialog asset %04X bytes:", (int)text_id);
        // Enough to cover a whole box's entry list, which is what shows how a
        // real asset terminates one.
        for (i = 0; i < 160; i++) {
            recomp_printf(" %02X", (int)(unsigned char)result[i]);
        }
        recomp_printf("\n");
    }

    if (sHijack && text_id == SPEAK_CARRIER_ASSET) {
        sHijack = 0;
        return (char*)sBlob;
    }

    return result;
}

// Builds the blob for one message.
//
// Our text goes in the SECOND (top) box on purpose. `len` is a byte count whose
// exact meaning -- whether it includes the trailing NUL -- can only be confirmed
// against a real asset, and getting it wrong would misalign everything parsed
// afterwards. Putting our string last means nothing is parsed afterwards, so an
// off-by-one there is harmless. Move it to the bottom box once the dump printed
// by the patched dialogBin_get confirms the encoding.
//
// The unused box gets a single "close" entry (cmd -4) rather than a count of
// zero, because `loadAndCreateDialogs` reads element [0] unconditionally and a
// zero count would leave it reading a zero-sized allocation.
static void build_blob(const char* text, int portrait) {
    int len = twitch_strlen(text);
    int i = 0;
    int j;

    if (len > SPEAK_MAX_TEXT - 1) {
        len = SPEAK_MAX_TEXT - 1;
    }

    sBlob[i++] = 1;                                        // bottom: one entry
    sBlob[i++] = 4;                                        // cmd -4 == close
    sBlob[i++] = 1;
    sBlob[i++] = 0;

    sBlob[i++] = 2;                                        // top: text, then close
    // Setting the high bit keeps the byte out of the 0x01-0x1F range, which
    // loadDialogStrings would otherwise read as a command instead of a portrait.
    sBlob[i++] = (unsigned char)(((portrait - 0x0C) & 0x7F) | 0x80);
    sBlob[i++] = (unsigned char)(len + 1);
    for (j = 0; j < len; j++) {
        // The dialogue font has no lowercase glyphs -- real assets store their
        // text in caps ("DINGPOT, DINGPOT..."), and anything lowercase renders
        // as a blank. Upper-casing here keeps the overlay showing the message as
        // the chatter actually typed it.
        char c = text[j];
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        sBlob[i++] = (unsigned char)c;
    }
    sBlob[i++] = 0;

    // The terminator. Without it the state machine advances past the end of the
    // array `loadAndCreateDialogs` allocated, reads whatever follows as the next
    // portrait and string, and leaves that box open forever.
    sBlob[i++] = 4;                                        // cmd -4 == close
    sBlob[i++] = 1;
    sBlob[i++] = 0;
}

void speak_queue(const char* user, const char* text, int default_portrait, int show_name) {
    SpeakRequest* req;
    const char* body = text;
    int portrait = default_portrait;
    int slot;
    int i;

    // Look for a "name:" prefix and pull the character out of it.
    for (i = 0; i < 24 && text[i] != '\0'; i++) {
        if (text[i] == ':') {
            char candidate[24];
            int found;

            twitch_strcpy(candidate, text, i + 1);
            found = speak_lookup_portrait(candidate);
            if (found >= 0) {
                portrait = found;
                body = text + i + 1;
                while (*body == ' ') {
                    body++;
                }
            }
            break;
        }
    }

    if (*body == '\0') {
        return;
    }

    // Drop the oldest request rather than the newest -- a backlog of stale chat
    // is less interesting than what just arrived.
    if (sQueueCount == SPEAK_QUEUE_SIZE) {
        sQueueHead = (sQueueHead + 1) % SPEAK_QUEUE_SIZE;
        sQueueCount--;
    }

    slot = (sQueueHead + sQueueCount) % SPEAK_QUEUE_SIZE;
    req = &sQueue[slot];
    req->portrait = portrait;

    if (show_name) {
        twitch_strcpy(req->text, user, SPEAK_MAX_TEXT);
        twitch_strcat(req->text, ": ", SPEAK_MAX_TEXT);
        twitch_strcat(req->text, body, SPEAK_MAX_TEXT);
    } else {
        twitch_strcpy(req->text, body, SPEAK_MAX_TEXT);
    }

    sQueueCount++;
}

void speak_tick(void) {
    SpeakRequest* req;

    if (sQueueCount == 0) {
        return;
    }

    // D_8027A130 == 3 is the in-game state that mainLoop runs the world update
    // in; anything else (boot, file select, cutscene transitions) has no
    // dialogue system to talk to.
    if (D_8027A130 != 3 || getGameMode() != GAME_MODE_3_NORMAL) {
        return;
    }

    if (gcdialog_hasCurrentTextId()) {
        return;
    }

    req = &sQueue[sQueueHead];
    build_blob(req->text, req->portrait);

    sHijack = 1;
    if (!gcdialog_showDialog(SPEAK_CARRIER_ASSET, 0, NULL, NULL, NULL, NULL)) {
        // The game refused (a cutscene flag, most likely). Leave the message
        // queued and try again next frame.
        sHijack = 0;
        return;
    }

    sQueueHead = (sQueueHead + 1) % SPEAK_QUEUE_SIZE;
    sQueueCount--;
}
