/*
 * inkstation — native PocketBook .app showing live UK train departures and
 * arrivals from the Realtime Trains API.
 *
 * Runs on stock PocketBook firmware via the official InkView SDK: no KOReader,
 * no jailbreak. Install by copying inkstation.app onto the SD card under
 * applications/.
 *
 * This file is the InkView entry point. It loads the RTT credential from the
 * on-device config, owns the fonts and the event loop, and forwards every
 * event to the active screen on the navigation stack (see app.h / screens.h).
 */

#include <stddef.h>

#include "inkview.h"

#include "app.h"
#include "config.h"
#include "net.h"
#include "rtt.h"
#include "screens.h"
#include "ui.h"

static void load_credential(void)
{
    char tok[CONFIG_VALUE_MAX];
    if (config_get(CONFIG_KEY_RTT_TOKEN, tok, sizeof tok) == 0)
        rtt_set_credential(tok);
}

static int inkstation_handler(int type, int par1, int par2)
{
    switch (type) {
    case EVT_INIT:
        ui_fonts_open();
        load_credential();
        /* Bring WiFi up at launch. The firmware still powers the radio down on
         * its idle timer, so each fetch re-asserts the link via
         * net_ensure_online() before use to recover from a mid-session drop. */
        net_ensure_online();
        nav_push(screen_search());   /* paints the first screen */
        return 1;

    case EVT_SHOW:
        nav_repaint();
        return 1;

    case EVT_KEYPRESS: {
        screen_t *cur = nav_current();
        if (cur && cur->on_key) return cur->on_key(cur, par1);
        return 0;
    }

    case EVT_POINTERUP: {
        screen_t *cur = nav_current();
        if (cur && cur->on_pointer) return cur->on_pointer(cur, par1, par2);
        return 0;
    }

    case EVT_EXIT:
        ui_fonts_close();
        return 1;

    default:
        return 0;
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    InkViewMain(inkstation_handler);
    return 0;
}
