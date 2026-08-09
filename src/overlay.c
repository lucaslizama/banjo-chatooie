// The on-screen chat panel.
//
// All the elements are created once and then reused: each row keeps a coloured
// username label and a message label, and a new message shifts the text up by
// one row. Creating and destroying elements per message would churn the UI tree
// every time chat moves, which for a busy channel is several times a second.
//
// Every mutation is bracketed by recompui_open_context / recompui_close_context.
// Opening the context takes its lock, which is what keeps these edits from
// landing while the renderer is walking the same tree.

#include "overlay.h"

#include "modding.h"
#include "recompui.h"
#include "recomputils.h"

typedef struct {
    RecompuiResource row;
    RecompuiResource user_label;
    RecompuiResource text_label;
} OverlayRow;

static RecompuiContext sContext = RECOMPUI_NULL_CONTEXT;
static RecompuiResource sPanel = RECOMPUI_NULL_RESOURCE;
static OverlayRow sRows[OVERLAY_MAX_LINES];

// Ring of lines, oldest first. Kept alongside the UI so a settings change can
// re-lay-out the rows without losing the backlog.
static char sUsers[OVERLAY_MAX_LINES][TWITCH_USER_CAPACITY + 2]; // room for ": "
static char sTexts[OVERLAY_MAX_LINES][TWITCH_TEXT_CAPACITY];
static int sColors[OVERLAY_MAX_LINES];
static int sUsed = 0;

static OverlaySettings sSettings;

static const RecompuiColor kPanelText = { 235, 235, 235, 255 };
static const RecompuiColor kStatusText = { 170, 170, 170, 255 };
// Twitch purple, used for chatters who never picked a colour.
static const RecompuiColor kDefaultUser = { 145, 70, 255, 255 };

// The mod is built with -nostdinc, so there is no libc to lean on.
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

static void str_append(char* dst, const char* src, int capacity) {
    int len = 0;
    while (dst[len] != '\0' && len < capacity - 1) {
        len++;
    }
    str_copy(dst + len, src, capacity - len);
}

static RecompuiColor color_from_rgb(int rgb) {
    RecompuiColor color = kDefaultUser;
    if (rgb != TWITCH_COLOR_NONE) {
        color.r = (unsigned char)((rgb >> 16) & 0xFF);
        color.g = (unsigned char)((rgb >> 8) & 0xFF);
        color.b = (unsigned char)(rgb & 0xFF);
        color.a = 255;
    }
    return color;
}

// Pushes the panel into the requested corner. Only the two relevant edges get an
// offset; the other two are left unset so the panel keeps its own size.
static void apply_corner(unsigned long corner) {
    const float margin = 24.0f;

    recompui_set_position(sPanel, POSITION_ABSOLUTE);

    switch (corner) {
        case OVERLAY_CORNER_TOP_LEFT:
            recompui_set_left(sPanel, margin, UNIT_DP);
            recompui_set_top(sPanel, margin, UNIT_DP);
            break;
        case OVERLAY_CORNER_TOP_RIGHT:
            recompui_set_right(sPanel, margin, UNIT_DP);
            recompui_set_top(sPanel, margin, UNIT_DP);
            break;
        case OVERLAY_CORNER_BOTTOM_RIGHT:
            recompui_set_right(sPanel, margin, UNIT_DP);
            recompui_set_bottom(sPanel, margin, UNIT_DP);
            break;
        case OVERLAY_CORNER_BOTTOM_LEFT:
        default:
            recompui_set_left(sPanel, margin, UNIT_DP);
            recompui_set_bottom(sPanel, margin, UNIT_DP);
            break;
    }
}

static void apply_settings_locked(const OverlaySettings* settings) {
    RecompuiColor background = { 0, 0, 0, 0 };
    unsigned long i;

    sSettings = *settings;
    if (sSettings.line_count < 1) {
        sSettings.line_count = 1;
    }
    if (sSettings.line_count > OVERLAY_MAX_LINES) {
        sSettings.line_count = OVERLAY_MAX_LINES;
    }

    background.a = (unsigned char)(sSettings.background_opacity * 255.0f);
    recompui_set_background_color(sPanel, &background);

    apply_corner(sSettings.corner);

    for (i = 0; i < OVERLAY_MAX_LINES; i++) {
        recompui_set_font_size(sRows[i].user_label, sSettings.text_size, UNIT_DP);
        recompui_set_font_size(sRows[i].text_label, sSettings.text_size, UNIT_DP);
        // Rows past the configured count stay in the tree but take up no space.
        recompui_set_display(sRows[i].row, i < sSettings.line_count ? DISPLAY_FLEX : DISPLAY_NONE);
    }
}

// Repaints every row from the sUsers / sTexts backlog. Rows with no line yet are
// left blank rather than hidden, so the panel doesn't jump around as chat fills in.
static void refresh_rows_locked(void) {
    unsigned long i;
    int first = sUsed - (int)sSettings.line_count;

    if (first < 0) {
        first = 0;
    }

    for (i = 0; i < sSettings.line_count; i++) {
        int index = first + (int)i;
        if (index < sUsed) {
            RecompuiColor user_color = color_from_rgb(sColors[index % OVERLAY_MAX_LINES]);
            recompui_set_text(sRows[i].user_label, sUsers[index % OVERLAY_MAX_LINES]);
            recompui_set_color(sRows[i].user_label, &user_color);
            recompui_set_text(sRows[i].text_label, sTexts[index % OVERLAY_MAX_LINES]);
            recompui_set_color(sRows[i].text_label, &kPanelText);
        } else {
            recompui_set_text(sRows[i].user_label, "");
            recompui_set_text(sRows[i].text_label, "");
        }
    }
}

void overlay_create(const OverlaySettings* settings) {
    unsigned long i;

    if (sContext != RECOMPUI_NULL_CONTEXT) {
        return;
    }

    sContext = recompui_create_context();
    recompui_open_context(sContext);

    sPanel = recompui_create_element(sContext, recompui_context_root(sContext));
    recompui_set_display(sPanel, DISPLAY_FLEX);
    recompui_set_flex_direction(sPanel, FLEX_DIRECTION_COLUMN);
    recompui_set_padding(sPanel, 12.0f, UNIT_DP);
    recompui_set_border_radius(sPanel, 8.0f, UNIT_DP);
    recompui_set_row_gap(sPanel, 4.0f, UNIT_DP);
    recompui_set_width(sPanel, 34.0f, UNIT_PERCENT);

    for (i = 0; i < OVERLAY_MAX_LINES; i++) {
        sRows[i].row = recompui_create_element(sContext, sPanel);
        recompui_set_display(sRows[i].row, DISPLAY_FLEX);
        recompui_set_flex_direction(sRows[i].row, FLEX_DIRECTION_ROW);
        recompui_set_align_items(sRows[i].row, ALIGN_ITEMS_FLEX_START);
        recompui_set_column_gap(sRows[i].row, 6.0f, UNIT_DP);

        sRows[i].user_label = recompui_create_label(sContext, sRows[i].row, "", LABELSTYLE_NORMAL);
        recompui_set_font_weight(sRows[i].user_label, 700);

        sRows[i].text_label = recompui_create_label(sContext, sRows[i].row, "", LABELSTYLE_NORMAL);
        // The message takes the rest of the row and wraps; the name never shrinks.
        recompui_set_flex_grow(sRows[i].text_label, 1.0f);
        recompui_set_flex_shrink(sRows[i].text_label, 1.0f);
    }

    apply_settings_locked(settings);
    refresh_rows_locked();

    recompui_close_context(sContext);

    // The panel is decoration; it must never eat the player's input.
    recompui_set_context_captures_input(sContext, 0);
    recompui_set_context_captures_mouse(sContext, 0);
}

void overlay_apply_settings(const OverlaySettings* settings) {
    if (sContext == RECOMPUI_NULL_CONTEXT) {
        return;
    }
    recompui_open_context(sContext);
    apply_settings_locked(settings);
    refresh_rows_locked();
    recompui_close_context(sContext);
}

void overlay_push_line(const char* user, const char* text, int color) {
    int slot;

    if (sContext == RECOMPUI_NULL_CONTEXT) {
        return;
    }

    slot = sUsed % OVERLAY_MAX_LINES;
    str_copy(sUsers[slot], user, TWITCH_USER_CAPACITY + 2);
    str_append(sUsers[slot], ":", TWITCH_USER_CAPACITY + 2);
    str_copy(sTexts[slot], text, TWITCH_TEXT_CAPACITY);
    sColors[slot] = color;
    sUsed++;

    recompui_open_context(sContext);
    refresh_rows_locked();
    recompui_close_context(sContext);
}

void overlay_set_status(const char* text) {
    unsigned long i;

    if (sContext == RECOMPUI_NULL_CONTEXT) {
        return;
    }

    sUsed = 0;

    recompui_open_context(sContext);
    for (i = 0; i < OVERLAY_MAX_LINES; i++) {
        recompui_set_text(sRows[i].user_label, "");
        recompui_set_text(sRows[i].text_label, i == 0 ? text : "");
        if (i == 0) {
            recompui_set_color(sRows[i].text_label, &kStatusText);
        }
    }
    recompui_close_context(sContext);
}

void overlay_show(void) {
    if (sContext != RECOMPUI_NULL_CONTEXT) {
        recompui_show_context(sContext);
    }
}

void overlay_hide(void) {
    if (sContext != RECOMPUI_NULL_CONTEXT) {
        recompui_hide_context(sContext);
    }
}
