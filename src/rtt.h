/*
 * rtt.h — Realtime Trains (RTT) API client for InkStation.
 *
 * Two layers, split so the parser can be unit-tested off-device:
 *   - rtt_parse(): pure JSON -> board model, no network, no globals.
 *   - rtt_fetch(): builds the request, performs it, then calls rtt_parse().
 *
 * Endpoint (the documented public REST API, https://www.realtimetrains.co.uk/
 * api/):
 *   departures: https://api.rtt.io/api/v1/json/search/{CRS}
 *   arrivals:   https://api.rtt.io/api/v1/json/search/{CRS}/arrivals
 *
 * Auth: the RTT API uses HTTP Basic auth (a `rttapi_*` username + password).
 * The credential is supplied once via rtt_set_credential() as "user:pass" and
 * is sent as Basic auth (embedded in the request URL's userinfo). For forward
 * compatibility a credential WITHOUT a ':' is instead sent as a Bearer token,
 * so a future token-style credential needs no code change. The credential is a
 * secret: it lives only in the on-device config file, never in the repo or the
 * binary (see config.h / README).
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
#define RTT_CRED_LEN     256

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
 * Parse an api.rtt.io /search JSON document into `out`, keeping only services
 * that have the leg relevant to `mode` (a booked departure for RTT_DEPARTURES,
 * a booked arrival for RTT_ARRIVALS). Pure: no network, no I/O. Returns 0 on
 * success, -1 on malformed JSON (message into err).
 */
int rtt_parse(const char *json, rtt_mode mode, rtt_board *out,
              char *err, size_t errsz);

/*
 * Set the API credential (copied). "user:pass" is sent as HTTP Basic auth; a
 * value with no ':' is sent as a Bearer token. Must be set before rtt_fetch();
 * typically loaded from config at startup. Passing NULL/"" clears it.
 */
void rtt_set_credential(const char *cred);

/* Returns 1 if a non-empty credential has been set. */
int rtt_has_credential(void);

/*
 * Fetch the live board for `crs` (a 3-letter CRS code) in `mode`. Returns 0 on
 * success (board filled), -1 on error (message into err).
 */
int rtt_fetch(const char *crs, rtt_mode mode, rtt_board *out,
              char *err, size_t errsz);

#endif /* INKSTATION_RTT_H */
