/*
 * rtt.h — Realtime Trains "Next Generation" API client for InkStation.
 *
 * Two layers, split so the parser can be unit-tested off-device:
 *   - rtt_parse(): pure JSON -> board model, no network, no globals.
 *   - rtt_fetch(): exchanges the refresh token for a short-lived access token,
 *     queries the location board, then calls rtt_parse().
 *
 * API (https://api-portal.rtt.io, spec at
 * https://realtimetrains.github.io/api-specification/), base https://data.rtt.io:
 *   token exchange: GET /api/get_access_token        (Bearer <refresh token>)
 *                   -> { "token": <access>, "validUntil": ... }
 *   location board: GET /gb-nr/location?code=<CRS>    (Bearer <access token>)
 *
 * The credential the user supplies (config key rtt_refresh_token) is the
 * long-life *refresh* token — a Bearer JWT issued by the RTT API portal. It is
 * exchanged for a short-life access token, which is cached and refreshed lazily
 * (on first use, and again whenever a board call returns 401). The classic
 * api.rtt.io/api/v1 REST API is a *different*, Basic-auth API and is NOT used.
 *
 * Only the gb-nr (GB National Rail) namespace is queried.
 */
#ifndef INKSTATION_RTT_H
#define INKSTATION_RTT_H

#include <stddef.h>

#define RTT_MAX_SERVICES 60
#define RTT_TIME_LEN     6     /* "HH:MM" + NUL */
#define RTT_EXP_LEN      16    /* "On time" / "HH:MM" / "Cancelled" */
#define RTT_PLACE_LEN    64
#define RTT_PLAT_LEN     8
#define RTT_OP_LEN       40
#define RTT_NAME_LEN     64
#define RTT_CRED_LEN     1024  /* refresh/access tokens are long JWTs */

typedef enum { RTT_DEPARTURES = 0, RTT_ARRIVALS = 1 } rtt_mode;

typedef struct {
    char time[RTT_TIME_LEN];    /* scheduled HH:MM */
    char expected[RTT_EXP_LEN]; /* "On time" | "HH:MM" | "Cancelled" */
    char place[RTT_PLACE_LEN];  /* destination (dep) or origin (arr) */
    char platform[RTT_PLAT_LEN];/* "" if unknown */
    char operator_[RTT_OP_LEN]; /* train operating company */
    int  cancelled;
    int  delayed;               /* realtime later than scheduled */
} rtt_service;

typedef struct {
    char name[RTT_NAME_LEN];    /* resolved station name from the response */
    char crs[8];
    rtt_mode mode;
    int  count;
    rtt_service services[RTT_MAX_SERVICES];
} rtt_board;

/*
 * Parse a /gb-nr/location JSON document into `out`, keeping only services that
 * have the leg relevant to `mode` (a departure for RTT_DEPARTURES, an arrival
 * for RTT_ARRIVALS). Pure: no network, no I/O. Returns 0 on success, -1 on
 * malformed JSON (message into err).
 */
int rtt_parse(const char *json, rtt_mode mode, rtt_board *out,
              char *err, size_t errsz);

/*
 * Set the long-life refresh token (copied). Must be called before rtt_fetch();
 * typically loaded from config at startup. Passing NULL/"" clears it (and any
 * cached access token).
 */
void rtt_set_credential(const char *cred);

/* Returns 1 if a non-empty refresh token has been set. */
int rtt_has_credential(void);

/*
 * Fetch the live board for `crs` (a 3-letter CRS code) in `mode`. Handles the
 * access-token exchange/refresh transparently. Returns 0 on success (board
 * filled), -1 on error (message into err).
 */
int rtt_fetch(const char *crs, rtt_mode mode, rtt_board *out,
              char *err, size_t errsz);

#endif /* INKSTATION_RTT_H */
