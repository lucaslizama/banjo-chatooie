#ifndef TWITCH_BOOTMAP_H
#define TWITCH_BOOTMAP_H

// Called once a frame. On the first few frames, redirects the boot sequence
// straight into the map chosen by the "debug_boot_map" config option. Does
// nothing when that option is Off, which is the default.
void bootmap_tick(void);

#endif // TWITCH_BOOTMAP_H
