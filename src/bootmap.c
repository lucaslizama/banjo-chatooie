// Optional developer shortcut: jump straight into a map on launch.
//
// Testing anything that needs a loaded level otherwise means sitting through the
// Rareware intro and the file select every single run.
//
// This patches the game's own choice of boot map rather than trying to redirect
// the boot afterwards. `core1_init` runs
//
//     setBootMap(getDefaultBootMap());
//     ...
//     func_8023DA9C(3);
//
// all before `mainThread_entry` enters its `while (1) mainLoop()` loop, so by the
// time any per-frame hook could act, the game is already in its boot map and
// pulling it somewhere else means interrupting a transition that is underway.
// Replacing `getDefaultBootMap` instead means the game boots where we want it to
// the first time, through its own code path.
//
// This redirects the game to a scratch save file before it can touch anything.
//
// That is not a nicety. Booting straight into a map skips the file select, so
// the game's in-memory save state is blank -- and Banjo-Kazooie writes that
// state out on its own, on entering the lair and at other checkpoints. Pointed
// at the normal save file, it overwrites a real save with a blank one, and then
// overwrites the backup with it too. That destroyed a save during testing before
// this call was added. Never remove it.

#include "bootmap.h"

#include "modding.h"
#include "recompconfig.h"
#include "enums.h"
#include "PR/ultratypes.h"

// Menu order of the "debug_boot_map" config option. Index 0 is Off.
static const s32 kBootMaps[] = {
    0,
    MAP_1_SM_SPIRAL_MOUNTAIN,
    MAP_2_MM_MUMBOS_MOUNTAIN,
    MAP_7_TTC_TREASURE_TROVE_COVE,
    MAP_B_CC_CLANKERS_CAVERN,
    MAP_D_BGS_BUBBLEGLOOP_SWAMP,
    MAP_69_GL_MM_LOBBY,
};

#define BOOT_MAP_COUNT ((int)(sizeof(kBootMaps) / sizeof(kBootMaps[0])))

RECOMP_PATCH s32 getDefaultBootMap(void) {
    unsigned long choice = recomp_get_config_u32("debug_boot_map");

    if (choice > 0 && choice < (unsigned long)BOOT_MAP_COUNT) {
        // Before the game can read or write anything, send it to a save file of
        // our own. See the note above: without this, a debug boot will wipe the
        // player's real save and its backup.
        recomp_change_save_file("debug_boot");
        return kBootMaps[choice];
    }

    // The stock value: the Rareware intro, which leads to the file select.
    return MAP_1F_CS_START_RAREWARE;
}
