// Optional developer shortcut: jump straight into a map on launch.
//
// Testing anything that needs a loaded level otherwise means sitting through the
// Rareware intro and the file select every single run. The game already has this
// exact mechanism for its own debug boot -- `func_8023DBDC` does
//
//     setBootMap(getSpecialBootMap());
//     func_8023DFF0(3);
//
// and that is precisely what this mirrors, with the map coming from a config
// option instead. Off by default, so it costs normal play nothing.
//
// No save file is loaded when booting this way, so the world comes up as an
// empty file. That is fine for testing and is what the game's own debug path
// does too.

#include "bootmap.h"

#include "modding.h"
#include "recompconfig.h"
#include "recomputils.h"
#include "enums.h"

extern void setBootMap(s32 map_id);
extern void func_8023DFF0(s32 state);
extern u32 D_8027A130;

// Menu order of the "debug_boot_map" config option.
static const s32 kBootMaps[] = {
    0,                              // Off
    MAP_1_SM_SPIRAL_MOUNTAIN,
    MAP_2_MM_MUMBOS_MOUNTAIN,
    MAP_7_TTC_TREASURE_TROVE_COVE,
    MAP_B_CC_CLANKERS_CAVERN,
    MAP_D_BGS_BUBBLEGLOOP_SWAMP,
    MAP_69_GL_MM_LOBBY,
};

#define BOOT_MAP_COUNT ((int)(sizeof(kBootMaps) / sizeof(kBootMaps[0])))

static int sDone = 0;
static int sFrames = 0;

void bootmap_tick(void) {
    unsigned long choice;

    if (sDone) {
        return;
    }

    // Give the game a few frames to finish coming up before redirecting it.
    sFrames++;
    if (sFrames < 4) {
        return;
    }

    sDone = 1;

    choice = recomp_get_config_u32("debug_boot_map");
    if (choice == 0 || choice >= (unsigned long)BOOT_MAP_COUNT) {
        return;
    }

    // Only redirect out of the boot sequence. If a level is somehow already
    // running, leave it alone rather than yanking the player out of it.
    if (D_8027A130 == 3) {
        return;
    }

    recomp_printf("[twitch-chat] debug boot to map 0x%02X\n", (int)kBootMaps[choice]);
    setBootMap(kBootMaps[choice]);
    func_8023DFF0(3);
}
