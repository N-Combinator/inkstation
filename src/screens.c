/*
 * screens.c — the station search and live board screens (see screens.h).
 *
 * Both screens are heap-allocated by their constructor and freed in
 * on_destroy. The board fetch is synchronous: InkView is single-threaded, so
 * the screen paints a "Loading…" frame, blocks on rtt_fetch(), then repaints
 * with the result. WiFi is re-asserted inside the HTTP layer on every request.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "inkview.h"

#include "app.h"
#include "config.h"
#include "dbg.h"
#include "net.h"
#include "rtt.h"
#include "screens.h"
#include "stations.h"
#include "ui.h"

/* ---- shared helpers ------------------------------------------------ */

/* Case-insensitive substring test (strcasestr is not portable here). */
static int contains_ci(const char *hay, const char *needle)
{
    if (!needle[0]) return 1;
    for (const char *h = hay; *h; h++) {
        const char *a = h, *b = needle;
        while (*a && *b &&
               tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
            a++; b++;
        }
        if (!*b) return 1;
    }
    return 0;
}

/* ================================================================== */
/* Station search (root screen)                                       */
/* ================================================================== */

/* Hold every station: with an empty query the list shows the whole bundle, so a
 * smaller cap silently truncated browsing (it stopped at the 250th station,
 * "Bingham"). Sized to the generated list so nothing is ever dropped. */
#define SEARCH_MAX STATION_COUNT
#define SEARCH_BAR_H 64

typedef struct {
    char query[64];
    int results[SEARCH_MAX];          /* indices into STATIONS */
    int n;
    ui_list list;
    ui_list_item items[SEARCH_MAX];
    char secondary[SEARCH_MAX][16];   /* CRS text per row */
    int want_kbd;                     /* auto-open the keyboard once, post-init */
    int shows_seen;                   /* on_show calls so far (defer past init) */
} search_data;

/* The InkView keyboard callback has no user pointer, so the active search
 * screen registers itself here for the duration of an open keyboard. */
static search_data *g_active_search;

static void search_open_keyboard(screen_t *self);

static void search_rebuild(search_data *d)
{
    d->n = 0;
    for (int i = 0; i < STATION_COUNT && d->n < SEARCH_MAX; i++) {
        if (contains_ci(STATIONS[i].name, d->query) ||
            contains_ci(STATIONS[i].crs, d->query)) {
            d->results[d->n] = i;
            snprintf(d->secondary[d->n], sizeof d->secondary[d->n],
                     "%s", STATIONS[i].crs);
            d->items[d->n].primary = STATIONS[i].name;
            d->items[d->n].secondary = d->secondary[d->n];
            d->n++;
        }
    }
    ui_list_init(&d->list, d->items, d->n);
    ui_list_set_top_inset(&d->list, SEARCH_BAR_H);
}

static void search_draw(screen_t *self)
{
    search_data *d = self->data;
    const ui_fonts *f = ui_get_fonts();
    int w = ScreenWidth();

    ui_draw_header("InkStation");

    /* Search bar just below the header. */
    int by = ui_header_height();
    FillArea(0, by, w, SEARCH_BAR_H, WHITE);
    DrawRect(16, by + 8, w - 32, SEARCH_BAR_H - 16, BLACK);
    SetFont(f->sub, d->query[0] ? BLACK : DGRAY);
    char bar[96];
    snprintf(bar, sizeof bar, "  \xF0\x9F\x94\x8D  %s",
             d->query[0] ? d->query : "Tap to search a station");
    DrawTextRect(24, by + 8, w - 48, SEARCH_BAR_H - 16, bar,
                 ALIGN_LEFT | VALIGN_MIDDLE);

    ui_list_draw(&d->list);

    /* Visible debug indicator: shows the captured query and how many stations
     * matched, so a tester can confirm keystrokes reach the filter on-device. */
    char foot[160];
    snprintf(foot, sizeof foot, "q=\"%s\"  matches=%d   \xE2\x80\xA2   OK: open  \xE2\x80\xA2  tap bar: edit",
             d->query, d->n);
    ui_draw_footer(foot);

    ui_flush_full();

    /* Auto-open the keyboard once, but only AFTER the app has finished
     * initialising. OpenKeyboard() called from within EVT_INIT (the first paint,
     * driven by nav_push in the init handler) silently does nothing on
     * PocketBook firmware — that was the "type a station and nothing happens"
     * bug. The first on_show runs inside EVT_INIT; the keyboard is opened on the
     * next one (the real EVT_SHOW), when the event loop is live. A tap or OK key
     * is the manual fallback. */
    d->shows_seen++;
    if (d->want_kbd && d->shows_seen >= 2) {
        d->want_kbd = 0;
        search_open_keyboard(self);
    }
}

static void search_kbd_cb(char *text)
{
    if (!g_active_search) return;

    /* OpenKeyboard was given d->query as its text buffer, so `text` is normally
     * that SAME pointer. snprintf(query, "%s", text) would then copy a buffer
     * onto itself (overlapping src/dst == undefined behaviour) — on the device
     * that left the query blank, so the filter matched every station and the
     * list never changed. Copy through a temp so src and dst never alias; if the
     * firmware handed back a different (non-aliasing) buffer this still works. */
    if (text) {
        char tmp[sizeof g_active_search->query];
        snprintf(tmp, sizeof tmp, "%s", text);
        snprintf(g_active_search->query, sizeof g_active_search->query,
                 "%s", tmp);
    }
    search_rebuild(g_active_search);
    dbg_log("search: query='%s' -> %d match(es)",
            g_active_search->query, g_active_search->n);

    screen_t *cur = nav_current();
    if (cur && cur->data == g_active_search && cur->on_show)
        cur->on_show(cur);
}

static void search_open_keyboard(screen_t *self)
{
    search_data *d = self->data;
    g_active_search = d;
    dbg_log("search: opening keyboard (query='%s')", d->query);
    OpenKeyboard("Search station", d->query,
                 (int)sizeof(d->query) - 1, 0, search_kbd_cb);
}

static void search_open_selected(screen_t *self)
{
    search_data *d = self->data;
    if (d->n == 0) {
        /* Nothing to open yet — treat OK as "let me search". */
        search_open_keyboard(self);
        return;
    }
    int idx = d->results[d->list.selected];
    dbg_log("search: selected '%s' -> CRS %s",
            STATIONS[idx].name, STATIONS[idx].crs);
    nav_push(screen_board(STATIONS[idx].crs, STATIONS[idx].name,
                          RTT_DEPARTURES));
}

static void search_on_enter(screen_t *self)
{
    search_data *d = self->data;
    g_active_search = d;
    /* Arm the auto-keyboard for the first visit (no query yet). It is actually
     * opened from search_draw on the first post-init paint — see the comment
     * there for why it cannot be opened here (inside EVT_INIT). */
    if (!d->query[0]) d->want_kbd = 1;
}

static int search_on_key(screen_t *self, int key)
{
    search_data *d = self->data;
    /* The Menu key is a dedicated "search" trigger on models that have it. */
    if (key == IV_KEY_MENU) { search_open_keyboard(self); return 1; }

    switch (ui_nav_classify(key)) {
    case UI_NAV_UP:        if (ui_list_move(&d->list, -1)) search_draw(self); return 1;
    case UI_NAV_DOWN:      if (ui_list_move(&d->list, +1)) search_draw(self); return 1;
    case UI_NAV_PAGE_UP:   if (ui_list_page(&d->list, -1)) search_draw(self); return 1;
    case UI_NAV_PAGE_DOWN: if (ui_list_page(&d->list, +1)) search_draw(self); return 1;
    case UI_NAV_SELECT:    search_open_selected(self); return 1;
    /* The search screen is the root: Back/Home here closes the app. */
    case UI_NAV_BACK:      CloseApp(); return 1;
    default:               return 0;
    }
}

static int search_on_pointer(screen_t *self, int x, int y)
{
    search_data *d = self->data;
    if (ui_exit_button_hit(x, y)) {
        CloseApp();
        return 1;
    }
    int by = ui_header_height();
    /* Tap on the search bar -> edit the query. */
    if (y >= by && y < by + SEARCH_BAR_H) {
        search_open_keyboard(self);
        return 1;
    }
    int row = ui_list_hit(&d->list, x, y);
    if (row >= 0) {
        d->list.selected = row;
        search_open_selected(self);
        return 1;
    }
    return 0;
}

static void search_on_destroy(screen_t *self)
{
    if (g_active_search == self->data) g_active_search = NULL;
    free(self->data);
    free(self);
}

screen_t *screen_search(void)
{
    screen_t *s = calloc(1, sizeof *s);
    search_data *d = calloc(1, sizeof *d);
    if (!s || !d) { free(s); free(d); return NULL; }

    d->query[0] = '\0';
    search_rebuild(d);

    s->title = "InkStation";
    s->data = d;
    s->on_enter = search_on_enter;
    s->on_show = search_draw;
    s->on_key = search_on_key;
    s->on_pointer = search_on_pointer;
    s->on_destroy = search_on_destroy;
    return s;
}

/* ================================================================== */
/* Live departures / arrivals board                                   */
/* ================================================================== */

#define BOARD_LOADING 0
#define BOARD_OK      1
#define BOARD_ERROR   2

#define BTN_ROW_H 72
#define BTN_GAP   16

typedef struct {
    char crs[8];
    char name[RTT_NAME_LEN];
    rtt_mode mode;
    int state;
    char err[160];
    rtt_board board;
    ui_list list;
    ui_list_item items[RTT_MAX_SERVICES];
    char primary[RTT_MAX_SERVICES][96];
    char secondary[RTT_MAX_SERVICES][160];
} board_data;

static void board_title(board_data *d, char *out, size_t outsz)
{
    snprintf(out, outsz, "%s \xE2\x80\x94 %s",
             d->name[0] ? d->name : d->crs,
             d->mode == RTT_ARRIVALS ? "Arrivals" : "Departures");
}

static void board_build_items(board_data *d)
{
    int n = d->board.count;
    for (int i = 0; i < n; i++) {
        const rtt_service *s = &d->board.services[i];

        snprintf(d->primary[i], sizeof d->primary[i], "%-5s  %s",
                 s->time, s->place[0] ? s->place : "(unknown)");

        /* Secondary: platform, status, operator. */
        char plat[24] = "";
        if (s->platform[0]) snprintf(plat, sizeof plat, "Plat %s", s->platform);

        const char *status = s->expected;
        snprintf(d->secondary[i], sizeof d->secondary[i], "%s%s%s%s%s",
                 plat,
                 plat[0] ? "  \xE2\x80\xA2  " : "",
                 status,
                 s->operator_[0] ? "  \xE2\x80\xA2  " : "",
                 s->operator_);

        d->items[i].primary = d->primary[i];
        d->items[i].secondary = d->secondary[i];
    }
    ui_list_init(&d->list, d->items, n);
    ui_list_set_bottom_inset(&d->list, BTN_ROW_H);
}

/* Geometry of the two action buttons (toggle | refresh) above the footer. */
static void board_buttons(int *tx, int *rx, int *by, int *bw, int *bh)
{
    int w = ScreenWidth();
    int margin = 16;
    *by = ScreenHeight() - ui_footer_height() - BTN_ROW_H + 8;
    *bh = BTN_ROW_H - 16;
    *bw = (w - 2 * margin - BTN_GAP) / 2;
    *tx = margin;
    *rx = margin + *bw + BTN_GAP;
}

static void board_draw_buttons(board_data *d)
{
    const ui_fonts *f = ui_get_fonts();
    int tx, rx, by, bw, bh;
    board_buttons(&tx, &rx, &by, &bw, &bh);

    char toggle[32];
    snprintf(toggle, sizeof toggle, "Show %s",
             d->mode == RTT_ARRIVALS ? "Departures" : "Arrivals");

    DrawRect(tx, by, bw, bh, BLACK);
    DrawRect(tx + 1, by + 1, bw - 2, bh - 2, BLACK);
    SetFont(f->item, BLACK);
    DrawTextRect(tx, by, bw, bh, toggle, ALIGN_CENTER | VALIGN_MIDDLE);

    DrawRect(rx, by, bw, bh, BLACK);
    DrawRect(rx + 1, by + 1, bw - 2, bh - 2, BLACK);
    DrawTextRect(rx, by, bw, bh, "Refresh", ALIGN_CENTER | VALIGN_MIDDLE);
}

static void board_draw(screen_t *self)
{
    board_data *d = self->data;
    char title[96];
    board_title(d, title, sizeof title);
    ui_draw_header(title);

    if (d->state == BOARD_LOADING) {
        ui_draw_message("Loading\xE2\x80\xA6", d->name);
        ui_draw_footer("");
        ui_flush_full();
        return;
    }
    if (d->state == BOARD_ERROR) {
        ui_draw_message("Couldn't load board", d->err);
        board_draw_buttons(d);
        ui_draw_footer("OK: retry   \xE2\x80\xA2   \xE2\x80\xB9 Back");
        ui_flush_full();
        return;
    }

    if (d->board.count == 0)
        ui_draw_message("No services", d->mode == RTT_ARRIVALS
                                           ? "Nothing arriving soon"
                                           : "Nothing departing soon");
    else
        ui_list_draw(&d->list);

    board_draw_buttons(d);
    ui_draw_footer("OK: refresh   \xE2\x80\xA2   \xE2\x86\x90/\xE2\x86\x92: Dep/Arr");
    ui_flush_full();
}

/* Blocking fetch. Paints a Loading frame first so the user gets feedback. */
static void board_refresh(screen_t *self)
{
    board_data *d = self->data;

    d->state = BOARD_LOADING;
    board_draw(self);   /* shows "Loading…" immediately */

    dbg_log("board: fetch CRS=%s mode=%s token=%s",
            d->crs, d->mode == RTT_ARRIVALS ? "arrivals" : "departures",
            rtt_has_credential() ? "set" : "MISSING");
    /* Assert WiFi and WAIT for the link to actually come up before firing the
     * request. The firmware powers the radio down on idle and NetConnect()
     * returns before the interface is routable, so a request sent immediately
     * is refused at once ("connection refused after 5ms"). net_wait_online()
     * re-asserts and polls the connection state; http_get() also re-asserts and
     * settles between retries as a backstop. */
    net_ensure_online();
    int online = net_wait_online(NET_WAIT_TIMEOUT_MS, NET_WAIT_POLL_MS);
    dbg_log("board: WiFi %s before fetch", online ? "online" : "NOT online");
    char err[160] = {0};
    int rc = rtt_fetch(d->crs, d->mode, &d->board, err, sizeof err);
    if (rc != 0) {
        d->state = BOARD_ERROR;
        snprintf(d->err, sizeof d->err, "%s", err);
        dbg_log("board: fetch FAILED — %s", err);
    } else {
        d->state = BOARD_OK;
        board_build_items(d);
        dbg_log("board: fetch ok — %d service(s)", d->board.count);
        /* Remember the last station for next launch. */
        config_set(CONFIG_KEY_LAST_CRS, d->crs);
    }
    board_draw(self);
}

static void board_toggle(screen_t *self)
{
    board_data *d = self->data;
    d->mode = (d->mode == RTT_ARRIVALS) ? RTT_DEPARTURES : RTT_ARRIVALS;
    board_refresh(self);
}

static void board_on_enter(screen_t *self)
{
    board_refresh(self);
}

static int board_on_key(screen_t *self, int key)
{
    board_data *d = self->data;
    switch (key) {
    case IV_KEY_OK:
        board_refresh(self);
        return 1;
    case IV_KEY_LEFT:
    case IV_KEY_RIGHT:
        board_toggle(self);
        return 1;
    case IV_KEY_UP:
        if (ui_list_move(&d->list, -1)) board_draw(self);
        return 1;
    case IV_KEY_DOWN:
        if (ui_list_move(&d->list, +1)) board_draw(self);
        return 1;
    case IV_KEY_PREV:
    case IV_KEY_PREV2:
        if (ui_list_page(&d->list, -1)) board_draw(self);
        return 1;
    case IV_KEY_NEXT:
    case IV_KEY_NEXT2:
        if (ui_list_page(&d->list, +1)) board_draw(self);
        return 1;
    case IV_KEY_BACK:
    case IV_KEY_HOME:
        nav_pop();
        return 1;
    default:
        return 0;
    }
}

static int board_on_pointer(screen_t *self, int x, int y)
{
    if (ui_back_button_hit(x, y)) { nav_pop(); return 1; }

    int tx, rx, by, bw, bh;
    board_buttons(&tx, &rx, &by, &bw, &bh);
    if (y >= by && y <= by + bh) {
        if (x >= tx && x <= tx + bw) { board_toggle(self); return 1; }
        if (x >= rx && x <= rx + bw) { board_refresh(self); return 1; }
    }
    return 0;
}

static void board_on_destroy(screen_t *self)
{
    free(self->data);
    free(self);
}

screen_t *screen_board(const char *crs, const char *name, rtt_mode mode)
{
    screen_t *s = calloc(1, sizeof *s);
    board_data *d = calloc(1, sizeof *d);
    if (!s || !d) { free(s); free(d); return NULL; }

    snprintf(d->crs, sizeof d->crs, "%s", crs ? crs : "");
    snprintf(d->name, sizeof d->name, "%s", name ? name : "");
    d->mode = mode;
    d->state = BOARD_LOADING;

    s->title = "Board";
    s->data = d;
    s->on_enter = board_on_enter;
    s->on_show = board_draw;
    s->on_key = board_on_key;
    s->on_pointer = board_on_pointer;
    s->on_destroy = board_on_destroy;
    return s;
}
