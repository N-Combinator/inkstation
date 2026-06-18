/*
 * ui.h — InkView drawing helpers and a reusable list widget.
 *
 * Keeps all the e-ink layout/painting concerns in one place so screens can be
 * written in terms of "draw a header", "draw this list", "draw a footer hint"
 * rather than juggling raw InkView coordinates. The list widget backs both the
 * station search results and the live departures/arrivals board. It pages
 * through long lists with the hardware page-turn keys.
 *
 * Adapted from inkshelf's ui layer.
 */

#ifndef INKSTATION_UI_H
#define INKSTATION_UI_H

#include "inkview.h"

/* Shared fonts, opened once at startup. */
typedef struct {
    ifont *title;   /* header bar */
    ifont *item;    /* list item primary line */
    ifont *sub;     /* list item secondary line / hints */
} ui_fonts;

void ui_fonts_open(void);
void ui_fonts_close(void);
const ui_fonts *ui_get_fonts(void);

/* Chrome. Header draws the title bar; footer draws a centred hint line. The
 * header automatically shows an on-screen "Back" button whenever the nav stack
 * is deeper than the root screen, so Back is reachable by touch on any
 * PocketBook regardless of its hardware key layout (some models are
 * touch-only). Screens route taps through ui_back_button_hit(). */
int  ui_header_height(void);
int  ui_footer_height(void);
void ui_draw_header(const char *title);
void ui_draw_footer(const char *hint);

/* Returns non-zero if (x, y) falls on the on-screen Back button. Always 0 on
 * the root screen (no button is drawn there). */
int  ui_back_button_hit(int x, int y);

/* Push the whole framebuffer to the panel (full e-ink refresh). */
void ui_flush_full(void);

/* Centred message filling the content area (loading / error / empty states). */
void ui_draw_message(const char *line1, const char *line2);

/* ---- List widget --------------------------------------------------- */

typedef struct {
    const char *primary;     /* required: main line */
    const char *secondary;   /* optional: smaller second line, may be NULL */
} ui_list_item;

typedef struct {
    const ui_list_item *items;
    int count;
    int selected;       /* index of the highlighted row */
    int top;            /* index of the first visible row */
    int row_h;          /* pixel height of one row (incl. secondary line) */
    int area_y;         /* y of the list area (below header) */
    int area_h;         /* height of the list area (above footer) */
    int per_page;       /* rows that fit in area_h */
} ui_list;

/* Bind `items`/`count` to the list and compute geometry for the current screen
 * size. Selection starts at 0. */
void ui_list_init(ui_list *list, const ui_list_item *items, int count);

/* Reserve `px` pixels at the top of the list area (between the header and the
 * first row) for a screen-drawn widget such as a search/filter bar or a button
 * row, and recompute the visible-row geometry. Call right after ui_list_init(). */
void ui_list_set_top_inset(ui_list *list, int px);

/* Reserve `px` pixels at the BOTTOM of the list area (between the last row and
 * the footer) for a screen-drawn widget such as an action-button row. */
void ui_list_set_bottom_inset(ui_list *list, int px);

/* Draw the list rows within the content area (does not touch header/footer). */
void ui_list_draw(const ui_list *list);

/* Move the selection by `delta` rows (clamped), scrolling as needed. Returns
 * non-zero if the selection actually changed. */
int ui_list_move(ui_list *list, int delta);

/* Page the selection by one screenful in `dir` (+1 down, -1 up). Returns
 * non-zero if the selection changed. */
int ui_list_page(ui_list *list, int dir);

/* Map a touch point to a row index, or -1 if outside the list / empty row. */
int ui_list_hit(const ui_list *list, int x, int y);

/* ---- input classification ------------------------------------------ */

/* PocketBook models differ wildly in their hardware keys (some are touch-only,
 * some have page-turn bars, some have a d-pad). Map the raw InkView key code to
 * a single semantic navigation action so screens don't each re-encode the key
 * matrix. */
typedef enum {
    UI_NAV_NONE = 0,
    UI_NAV_UP,
    UI_NAV_DOWN,
    UI_NAV_PAGE_UP,
    UI_NAV_PAGE_DOWN,
    UI_NAV_SELECT,
    UI_NAV_BACK
} ui_nav_action;

ui_nav_action ui_nav_classify(int key);

#endif /* INKSTATION_UI_H */
