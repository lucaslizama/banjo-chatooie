// Renders chat messages through Banjo-Kazooie's own dialogue boxes.
//
// How this works
// --------------
// The game builds a dialogue from a byte blob returned by `dialogBin_get`:
//
//     [count][cmd][len][text NUL] x count     <- bottom box
//     [count][cmd][len][text NUL] x count     <- top box
//
// `len` counts the stored bytes including the terminator. Confirmed against a
// real asset, 0x0E57, whose first entry reads
//
//     07 B5 1F "DINGPOT, DINGPOT BY THE BENCH," 00
//        └ portrait  └ 0x1F = 31 = 30 characters + the NUL
//
// and whose text is upper-case throughout, because the dialogue font has no
// lowercase glyphs.
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

// The longest run of non-space characters we will hand to the game.
//
// `_gczoombox_findLineBreak` wraps by scanning BACKWARDS from the end of the
// string for a space that fits inside 24 printed characters -- and its loop has
// no lower bound on the index. Given a word longer than the line, it walks off
// the front of the string reading out of bounds until some earlier byte happens
// to equal ' ', then returns a negative length that the caller writes through:
//
//     this->unk60[this->unk15C] = 0;   // negative index
//
// The game's own dialogue never contains a word that long, so this never fires
// in normal play. Chat does it constantly, so we break long runs ourselves and
// guarantee a space inside every window the scan can look at.
#define SPEAK_MAX_WORD 18

// Highest valid portrait index. D_8036C6C0 runs to D_8036D924, which is 4708
// bytes, and gczoomboxPortraitInfo is 44 -- exactly 107 entries, 0x00 to 0x6A.
// Anything past this indexes off the end of the table into whatever follows.
#define SPEAK_MAX_PORTRAIT 0x6A

// Messages queued while a cutscene or another conversation is up wait rather
// than being thrown away, so this wants enough depth to cover a long cutscene
// without losing the messages sent at the start of it. At ~204 bytes an entry
// this is a few KB of BSS, which is cheap next to dropping someone's message.
#define SPEAK_QUEUE_SIZE 16

typedef struct {
    char text[SPEAK_MAX_TEXT];
    int portrait;
} SpeakRequest;

static SpeakRequest sQueue[SPEAK_QUEUE_SIZE];
static int sQueueHead = 0;
static int sQueueCount = 0;

// Blob handed to the game in place of the carrier asset's text.
// Sized for the worst case exactly: the 4-byte bottom entry, the top box's
// count/portrait/length header, a full-length string with its terminator, and
// the 3-byte close entry.
//
// Double buffered, and that matters: `loadDialogStrings` copies the cmd and len
// bytes into its own allocation but keeps `str` as a pointer straight into this
// buffer, which the box then reads from for as long as it is on screen. Building
// the next message into the same bytes would rewrite live text underneath it.
// Alternating means the outgoing message's string stays intact.
#define SPEAK_BLOB_SIZE (4 + 3 + SPEAK_MAX_TEXT + 3)
static unsigned char sBlobs[2][SPEAK_BLOB_SIZE];
static int sBlobIndex = 0;
#define sBlob (sBlobs[sBlobIndex])
static int sHijack = 0;

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
extern s32 map_get(void);
extern u32 D_8027A130;

// Cutscenes drive the dialogue system themselves, and injecting a box into the
// middle of one corrupts its state badly enough to crash the game. Rather than
// try to detect "a cutscene is mid-sequence", refuse to speak on any map that
// is a cutscene, the file select, or the Dingpot room.
static int is_safe_map(s32 map) {
    if (map < MAP_1_SM_SPIRAL_MOUNTAIN) {
        return 0;
    }
    if (map >= MAP_1E_CS_START_NINTENDO && map <= MAP_20_CS_END_NOT_100) {
        return 0;
    }
    if (map >= MAP_7B_CS_INTRO_GL_DINGPOT_1 && map <= MAP_8A_CS_INTRO_BANJOS_HOUSE_3) {
        return 0;
    }
    if (map >= MAP_91_FILE_SELECT) {
        return 0;
    }
    return 1;
}

// Frames the dialogue system must have been idle before we inject. A cutscene
// pauses between its own lines, and starting ours in one of those gaps is the
// same corruption as interrupting mid-line.
#define SPEAK_IDLE_FRAMES 30

static int sIdleFrames = 0;

typedef struct {
    const char* name;
    int portrait;
} PortraitEntry;

// Only portraits 0x0C and up are reachable: the blob's cmd byte maps to
// `cmd + 0xC`, so anything below that can't be addressed. Every major speaking
// character is above the line.
//
// CAUTION: these indices are NOT simply the decomp's GcZoomboxSprite values.
// Those names are community guesses and drift by one somewhere above 0x41 --
// the entry labelled BANJO_3 draws Tooty, and KAZOOIE_3 draws Banjo. Entries
// marked "verified" were checked against what the game actually renders; the
// rest are still the enum's values and may be off by one. Check one with the
// "#<index>" syntax before correcting it here.
static const PortraitEntry kPortraits[] = {
    /* verified */ { "banjo", 0x61 }, { "kazooie", 0x62 }, { "tooty", 0x60 },
    /* verified, from the Dingpot's line in asset 0x0E57 */ { "dingpot", 0x64 },
    /* verified, from Grunty's line in asset 0x0E57 */ { "grunty", 0x41 },
    { "gruntilda",  0x41 },
    { "bottles",    0x0F },
    { "mumbo",      0x10 }, { "brentilda",  0x57 }, { "cheato",     0x5B },
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
    { "lockup",     0x66 }, { "vile",       0x67 },
    { "jinjo",      0x20 }, { "yellowjinjo", 0x20 }, { "greenjinjo", 0x21 },
    { "bluejinjo",  0x22 }, { "pinkjinjo",  0x23 }, { "orangejinjo", 0x24 },
};

#define PORTRAIT_COUNT ((int)(sizeof(kPortraits) / sizeof(kPortraits[0])))

int speak_lookup_portrait(const char* name) {
    int i;

    // "#97" addresses a portrait by raw index, which is how a name gets checked
    // against what the game actually draws instead of against a guessed label.
    //
    // The real table has 107 entries, not the 106 the decomp's enum lists. Its
    // size is recoverable from the symbols: D_8036D924 - D_8036C6C0 = 4708
    // bytes, over a 44-byte gczoomboxPortraitInfo, is exactly 107. So one
    // portrait is missing from the enum and every name after the gap is one
    // index too low -- which is why "kazooie" drew Banjo.
    if (name[0] == '#') {
        int value = 0;
        int digits = 0;
        for (i = 1; name[i] >= '0' && name[i] <= '9'; i++) {
            value = value * 10 + (name[i] - '0');
            digits++;
        }
        if (digits > 0 && name[i] == '\0' && value >= 0x0C && value <= SPEAK_MAX_PORTRAIT) {
            return value;
        }
        return -1;
    }

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
// Maps a Unicode codepoint to a character the dialogue font can draw, or '\0'
// to drop it. This is a whitelist on purpose: the renderer only ever saw the
// bytes Rare put in the ROM, and anything above 0x7E is either the 0xFD escape
// or an index off the end of the font.
//
// Accented Latin letters fold to their base letter rather than disappearing, so
// "MAÑANA" reads as "MANANA" instead of "MAANA". Everything without a sensible
// stand-in -- emoji, CJK, symbols -- is dropped.
static char ascii_fold(unsigned int codepoint) {
    // U+00C0 to U+00FF, in order. 'x' stands in for the multiplication and
    // division signs, which are the only non-letters in the block.
    static const char kLatin1[] =
        "AAAAAAACEEEEIIII"      /* U+00C0 - U+00CF  À Á Â Ã Ä Å Æ Ç È É Ê Ë Ì Í Î Ï */
        "DNOOOOOxOUUUUYPS"      /* U+00D0 - U+00DF  Ð Ñ Ò Ó Ô Õ Ö × Ø Ù Ú Û Ü Ý Þ ß */
        "AAAAAAACEEEEIIII"      /* U+00E0 - U+00EF  à á â ã ä å æ ç è é ê ë ì í î ï */
        "DNOOOOOxOUUUUYPY";     /* U+00F0 - U+00FF  ð ñ ò ó ô õ ö ÷ ø ù ú û ü ý þ ÿ */

    if (codepoint >= 0x20 && codepoint <= 0x7E) {
        return (char)codepoint;
    }
    // U+0100 to U+017F, Latin Extended-A, in order. Covers the accented letters
    // Latin-1 misses -- ā ī ō, ł, ń, š, ž and friends.
    static const char kLatinExtA[] =
        "AAAAAACCCCCCCCDD"      /* U+0100 - U+010F */
        "DDEEEEEEEEEEGGGG"      /* U+0110 - U+011F */
        "GGGGHHHHIIIIIIII"      /* U+0120 - U+012F */
        "IIIIJJKKKLLLLLLL"      /* U+0130 - U+013F */
        "LLLNNNNNNNNNOOOO"      /* U+0140 - U+014F */
        "OOOORRRRRRSSSSSS"      /* U+0150 - U+015F */
        "SSTTTTTTUUUUUUUU"      /* U+0160 - U+016F */
        "UUUUWWYYYZZZZZZS";     /* U+0170 - U+017F */

    if (codepoint >= 0xC0 && codepoint <= 0xFF) {
        return kLatin1[codepoint - 0xC0];
    }
    if (codepoint >= 0x100 && codepoint <= 0x17F) {
        return kLatinExtA[codepoint - 0x100];
    }

    // Curly quotes and dashes are common in chat and have obvious equivalents.
    switch (codepoint) {
        case 0x2018: case 0x2019: return '\'';
        case 0x201C: case 0x201D: return '"';
        case 0x2013: case 0x2014: return '-';
        case 0x2026: return '.';
        case 0x00A0: return ' ';
        default: return '\0';
    }
}

// Returns how many characters made it into the box. Zero means the message was
// entirely characters the font can't draw -- all-emoji, or Japanese -- and there
// is nothing worth opening a box for.
static int build_blob(const char* text, int portrait) {
    int i = 0;
    int length_index;
    int written = 0;
    int run = 0;
    int j;

    sBlob[i++] = 1;                                        // bottom: one entry
    sBlob[i++] = 4;                                        // cmd -4 == close
    sBlob[i++] = 1;
    sBlob[i++] = 0;

    sBlob[i++] = 2;                                        // top: text, then close
    // Setting the high bit keeps the byte out of the 0x01-0x1F range, which
    // loadDialogStrings would otherwise read as a command instead of a portrait.
    sBlob[i++] = (unsigned char)(((portrait - 0x0C) & 0x7F) | 0x80);
    length_index = i++;              // filled in below, once the length is known

    for (j = 0; text[j] != '\0' && written < SPEAK_MAX_TEXT - 1; ) {
        unsigned char lead = (unsigned char)text[j];
        unsigned int codepoint;
        int continuations;
        char c;

        // Decode one UTF-8 character. Chat is UTF-8 and the overlay renders it
        // fine, but the dialogue box must not see a byte above 0x7E: 0xFD is an
        // escape the printer consumes along with the byte after it, and the rest
        // index off the end of the font. A message with accented letters in it
        // crashed the game outright.
        if (lead < 0x80) {
            codepoint = lead;
            continuations = 0;
        } else if ((lead & 0xE0) == 0xC0) {
            codepoint = lead & 0x1Fu;
            continuations = 1;
        } else if ((lead & 0xF0) == 0xE0) {
            codepoint = lead & 0x0Fu;
            continuations = 2;
        } else if ((lead & 0xF8) == 0xF0) {
            codepoint = lead & 0x07u;
            continuations = 3;
        } else {
            j++;            // stray continuation byte; nothing sensible to do
            continue;
        }

        j++;
        while (continuations-- > 0 && ((unsigned char)text[j] & 0xC0) == 0x80) {
            codepoint = (codepoint << 6) | ((unsigned char)text[j] & 0x3Fu);
            j++;
        }

        c = ascii_fold(codepoint);
        if (c == '\0') {
            continue;       // emoji and anything else with no ASCII stand-in
        }

        if (c == ' ') {
            run = 0;
        } else {
            // Force a break into any over-long run, so the backwards wrap scan
            // always finds a space within its window.
            if (run >= SPEAK_MAX_WORD) {
                sBlob[i++] = ' ';
                written++;
                run = 0;
                if (written >= SPEAK_MAX_TEXT - 1) {
                    break;
                }
            }
            run++;

            // The dialogue font has no lowercase glyphs -- real assets store
            // their text in caps ("DINGPOT, DINGPOT..."), and anything lowercase
            // renders as a blank. Upper-casing here keeps the overlay showing
            // the message as the chatter actually typed it.
            if (c >= 'a' && c <= 'z') {
                c = (char)(c - 'a' + 'A');
            }
        }

        sBlob[i++] = (unsigned char)c;
        written++;
    }

    sBlob[i++] = 0;
    sBlob[length_index] = (unsigned char)(written + 1);   // length includes the NUL

    // The terminator, and it is mandatory rather than tidy. The `default:` branch
    // of dialog_update reads `CMD(string_index + 1)->cmd` for every non-empty
    // text entry, to check for the -8/-9 conditional-text markers. So the game
    // always looks one entry past the one it is showing. Without an entry there,
    // it reads off the end of the array `loadAndCreateDialogs` allocated, takes
    // the garbage as the next portrait and string, and leaves that box open
    // forever -- which then blocks every later conversation, because
    // gcdialog_hasCurrentTextId() never goes false again.
    sBlob[i++] = 4;                                        // cmd -4 == close
    sBlob[i++] = 1;
    sBlob[i++] = 0;

    return written;
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

void speak_clear(void) {
    sQueueHead = 0;
    sQueueCount = 0;
}

void speak_tick(void) {
    SpeakRequest* req;

    // D_8027A130 == 3 is the in-game state that mainLoop runs the world update
    // in; anything else (boot, file select, cutscene transitions) has no
    // dialogue system to talk to.
    if (D_8027A130 != 3 || getGameMode() != GAME_MODE_3_NORMAL ||
        gcdialog_hasCurrentTextId() || !is_safe_map(map_get())) {
        sIdleFrames = 0;
        return;
    }

    if (sIdleFrames < SPEAK_IDLE_FRAMES) {
        sIdleFrames++;
        return;
    }

    if (sQueueCount == 0) {
        return;
    }

    req = &sQueue[sQueueHead];

    if (build_blob(req->text, req->portrait) == 0) {
        // Nothing the font can draw survived. Drop it rather than opening an
        // empty box for a couple of seconds.
        sQueueHead = (sQueueHead + 1) % SPEAK_QUEUE_SIZE;
        sQueueCount--;
        return;
    }

    sHijack = 1;
    if (!gcdialog_showDialog(SPEAK_CARRIER_ASSET, 0, NULL, NULL, NULL, NULL)) {
        // The game refused (a cutscene flag, most likely). Leave the message
        // queued, and require a fresh idle stretch before trying again.
        sHijack = 0;
        sIdleFrames = 0;
        return;
    }

    sIdleFrames = 0;

    // The box now holds pointers into the buffer we just filled, so the next
    // message must be built into the other one.
    sBlobIndex ^= 1;

    sQueueHead = (sQueueHead + 1) % SPEAK_QUEUE_SIZE;
    sQueueCount--;
}
