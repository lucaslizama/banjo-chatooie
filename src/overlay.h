#ifndef TWITCH_OVERLAY_H
#define TWITCH_OVERLAY_H

#include "twitch_chat_abi.h"

// Upper bound on the `line_count` config option. The rows are all created up
// front and shown or hidden as needed, so this only costs a few unused elements.
#define OVERLAY_MAX_LINES 10

typedef struct {
    unsigned long line_count;   // rows actually shown, 1..OVERLAY_MAX_LINES
    unsigned long corner;       // OVERLAY_CORNER_*
    float text_size;            // dp
    float background_opacity;   // 0..1
    float panel_width;          // percent of screen width
} OverlaySettings;

typedef enum {
    OVERLAY_CORNER_TOP_LEFT,
    OVERLAY_CORNER_TOP_RIGHT,
    OVERLAY_CORNER_BOTTOM_LEFT,
    OVERLAY_CORNER_BOTTOM_RIGHT
} OverlayCorner;

// Builds the UI. Safe to call once, from a recomp_on_init callback.
void overlay_create(const OverlaySettings* settings);

// Re-applies placement, sizing and row count without rebuilding the elements.
void overlay_apply_settings(const OverlaySettings* settings);

// Appends a line, scrolling the oldest one off the top.
// `color` is 0xRRGGBB or TWITCH_COLOR_NONE.
void overlay_push_line(const char* user, const char* text, int color);

// Replaces the visible lines with a single status line, e.g. "connecting...".
void overlay_set_status(const char* text);

void overlay_show(void);
void overlay_hide(void);

#endif // TWITCH_OVERLAY_H
