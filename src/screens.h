/*
 * screens.h — concrete screen constructors for InkStation.
 *
 * Each returns a pointer to a screen_t ready to hand to nav_push(). There are
 * two screens:
 *   - the station search (root): type a name, pick from the offline list;
 *   - the live board: departures/arrivals for the chosen station, with a
 *     Departures/Arrivals toggle and a Refresh action.
 */

#ifndef INKSTATION_SCREENS_H
#define INKSTATION_SCREENS_H

#include "app.h"
#include "rtt.h"

/* Root screen: station name search over the bundled offline list. */
screen_t *screen_search(void);

/* Live departures/arrivals board for `crs` (`name` is the display title).
 * Starts in `mode`; the user can toggle. The screen copies what it needs. */
screen_t *screen_board(const char *crs, const char *name, rtt_mode mode);

#endif /* INKSTATION_SCREENS_H */
