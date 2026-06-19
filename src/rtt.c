/*
 * rtt.c — Realtime Trains "Next Generation" API client (see rtt.h).
 *
 * rtt_parse() is pure (cJSON only) and host-testable. rtt_fetch() exchanges the
 * refresh token for an access token and queries the location board. The
 * response shape handled here is the documented Next Gen one:
 *
 *   { "query": { "location": { "description": "Guildford", "shortCodes": ["GLD"] } },
 *     "services": [
 *       { "scheduleMetadata": { "operator": { "name": "South Western Railway" } },
 *         "temporalData": {
 *           "departure": { "scheduleAdvertised": "2026-06-19T21:39:00",
 *                          "realtimeForecast":  "2026-06-19T21:39:00",
 *                          "isCancelled": false },
 *           "arrival":   { ... }, "displayAs": "CALL" },
 *         "locationMetadata": { "platform": { "planned": "3", "forecast": "3" } },
 *         "destination": [ { "location": { "description": "London Waterloo" } } ],
 *         "origin":      [ { "location": { "description": "Haslemere" } } ] }, ... ] }
 *
 * Times are ISO 8601 datetimes; we show HH:MM.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "http.h"
#include "rtt.h"

#define RTT_BASE        "https://data.rtt.io"
#define RTT_TOKEN_URL   RTT_BASE "/api/get_access_token"

static char g_refresh[RTT_CRED_LEN];   /* long-life refresh token from config */
static char g_access[RTT_CRED_LEN];    /* short-life access token (cached) */

void rtt_set_credential(const char *cred)
{
    if (cred && cred[0])
        snprintf(g_refresh, sizeof g_refresh, "%s", cred);
    else
        g_refresh[0] = '\0';
    g_access[0] = '\0';   /* drop any cached access token */
}

int rtt_has_credential(void)
{
    return g_refresh[0] != '\0';
}

static void set_err(char *err, size_t errsz, const char *msg)
{
    if (err && errsz) {
        strncpy(err, msg, errsz - 1);
        err[errsz - 1] = '\0';
    }
}

/* Copy a JSON string field into dst (NUL-terminated, truncated to dstsz). */
static void copy_str(char *dst, size_t dstsz, const cJSON *obj, const char *key)
{
    dst[0] = '\0';
    if (!obj) return;
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(it) && it->valuestring)
        snprintf(dst, dstsz, "%s", it->valuestring);
}

/* Extract "HH:MM" from an ISO 8601 datetime ("2026-06-19T21:39:00"). */
static void iso_to_hhmm(char *dst, size_t dstsz, const char *iso)
{
    dst[0] = '\0';
    if (!iso) return;
    const char *t = strchr(iso, 'T');
    if (t && strlen(t) >= 6)                 /* "T" + "HH:MM" */
        snprintf(dst, dstsz, "%c%c:%c%c", t[1], t[2], t[4], t[5]);
}

/* First element's location.description from a destination/origin array. */
static void copy_place(char *dst, size_t dstsz, const cJSON *svc,
                       const char *arr_key)
{
    dst[0] = '\0';
    const cJSON *arr = cJSON_GetObjectItemCaseSensitive(svc, arr_key);
    if (!cJSON_IsArray(arr)) return;
    const cJSON *first = cJSON_GetArrayItem(arr, 0);
    const cJSON *loc = cJSON_GetObjectItemCaseSensitive(first, "location");
    copy_str(dst, dstsz, loc, "description");
}

int rtt_parse(const char *json, rtt_mode mode, rtt_board *out,
              char *err, size_t errsz)
{
    if (!json || !out) { set_err(err, errsz, "no data"); return -1; }
    memset(out, 0, sizeof *out);
    out->mode = mode;

    cJSON *root = cJSON_Parse(json);
    if (!root) { set_err(err, errsz, "malformed response"); return -1; }

    const char *leg_key = (mode == RTT_DEPARTURES) ? "departure" : "arrival";
    const char *place_key = (mode == RTT_DEPARTURES) ? "destination" : "origin";

    /* Station name + CRS from query.location. */
    const cJSON *query = cJSON_GetObjectItemCaseSensitive(root, "query");
    const cJSON *loc = cJSON_GetObjectItemCaseSensitive(query, "location");
    copy_str(out->name, sizeof out->name, loc, "description");
    const cJSON *codes = cJSON_GetObjectItemCaseSensitive(loc, "shortCodes");
    if (cJSON_IsArray(codes)) {
        const cJSON *c0 = cJSON_GetArrayItem(codes, 0);
        if (cJSON_IsString(c0) && c0->valuestring)
            snprintf(out->crs, sizeof out->crs, "%s", c0->valuestring);
    }

    const cJSON *services = cJSON_GetObjectItemCaseSensitive(root, "services");
    if (cJSON_IsArray(services)) {
        const cJSON *svc = NULL;
        cJSON_ArrayForEach(svc, services) {
            if (out->count >= RTT_MAX_SERVICES) break;

            const cJSON *temporal =
                cJSON_GetObjectItemCaseSensitive(svc, "temporalData");
            const cJSON *leg =
                cJSON_GetObjectItemCaseSensitive(temporal, leg_key);
            if (!cJSON_IsObject(leg)) continue;   /* no relevant leg here */

            const cJSON *booked =
                cJSON_GetObjectItemCaseSensitive(leg, "scheduleAdvertised");
            if (!cJSON_IsString(booked) || !booked->valuestring ||
                !booked->valuestring[0])
                continue;

            rtt_service *s = &out->services[out->count];

            iso_to_hhmm(s->time, sizeof s->time, booked->valuestring);
            copy_place(s->place, sizeof s->place, svc, place_key);

            const cJSON *sched_meta =
                cJSON_GetObjectItemCaseSensitive(svc, "scheduleMetadata");
            const cJSON *op =
                cJSON_GetObjectItemCaseSensitive(sched_meta, "operator");
            copy_str(s->operator_, sizeof s->operator_, op, "name");

            const cJSON *loc_meta =
                cJSON_GetObjectItemCaseSensitive(svc, "locationMetadata");
            const cJSON *plat =
                cJSON_GetObjectItemCaseSensitive(loc_meta, "platform");
            copy_str(s->platform, sizeof s->platform, plat, "planned");
            if (!s->platform[0])
                copy_str(s->platform, sizeof s->platform, plat, "forecast");

            /* Cancellation: a boolean on the leg (and/or displayAs). */
            const cJSON *canc =
                cJSON_GetObjectItemCaseSensitive(leg, "isCancelled");
            char display[40];
            copy_str(display, sizeof display, temporal, "displayAs");
            s->cancelled = cJSON_IsTrue(canc) ||
                           (strstr(display, "CANCELLED") != NULL);

            const cJSON *rt =
                cJSON_GetObjectItemCaseSensitive(leg, "realtimeForecast");
            const char *rt_iso = (cJSON_IsString(rt) && rt->valuestring)
                                     ? rt->valuestring : NULL;

            if (s->cancelled) {
                snprintf(s->expected, sizeof s->expected, "Cancelled");
                s->delayed = 0;
            } else if (rt_iso && rt_iso[0]) {
                char rt_hhmm[RTT_TIME_LEN];
                iso_to_hhmm(rt_hhmm, sizeof rt_hhmm, rt_iso);
                if (strcmp(rt_hhmm, s->time) == 0) {
                    snprintf(s->expected, sizeof s->expected, "On time");
                    s->delayed = 0;
                } else {
                    snprintf(s->expected, sizeof s->expected, "%s", rt_hhmm);
                    /* ISO strings sort chronologically. */
                    s->delayed = strcmp(rt_iso, booked->valuestring) > 0;
                }
            } else {
                snprintf(s->expected, sizeof s->expected, "On time");
                s->delayed = 0;
            }

            out->count++;
        }
    }

    cJSON_Delete(root);
    return 0;
}

/* Exchange the refresh token for an access token (cached in g_access). */
static int rtt_exchange_token(char *err, size_t errsz)
{
    char *body = NULL;
    size_t blen = 0;
    long status = 0;
    char http_err[HTTP_ERR_LEN] = {0};

    int rc = http_get(RTT_TOKEN_URL, g_refresh, &body, &blen, &status,
                      http_err, sizeof http_err);
    if (rc != 0) {
        if (status == 401 || status == 403)
            set_err(err, errsz, "RTT rejected the token (check Settings)");
        else
            set_err(err, errsz, http_err[0] ? http_err : "token exchange failed");
        free(body);
        return -1;
    }

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) { set_err(err, errsz, "bad token response"); return -1; }

    const cJSON *tok = cJSON_GetObjectItemCaseSensitive(root, "token");
    int ok = 0;
    if (cJSON_IsString(tok) && tok->valuestring && tok->valuestring[0]) {
        snprintf(g_access, sizeof g_access, "%s", tok->valuestring);
        ok = 1;
    }
    cJSON_Delete(root);
    if (!ok) { set_err(err, errsz, "no access token in response"); return -1; }
    return 0;
}

/* GET the location board with the current access token. Returns the HTTP
 * status via *status so the caller can re-exchange on 401. */
static int rtt_get_board(const char *crs, char **body, size_t *blen,
                         long *status, char *err, size_t errsz)
{
    char url[128];
    snprintf(url, sizeof url, RTT_BASE "/gb-nr/location?code=%s", crs);
    return http_get(url, g_access, body, blen, status, err, errsz);
}

int rtt_fetch(const char *crs, rtt_mode mode, rtt_board *out,
              char *err, size_t errsz)
{
    if (!rtt_has_credential()) {
        set_err(err, errsz, "No RTT token set — see Settings");
        return -1;
    }
    if (!crs || !crs[0]) { set_err(err, errsz, "bad station code"); return -1; }

    /* Ensure we have an access token (lazy: exchange on first use). */
    if (!g_access[0] && rtt_exchange_token(err, errsz) != 0)
        return -1;

    char *body = NULL;
    size_t blen = 0;
    long status = 0;
    char http_err[HTTP_ERR_LEN] = {0};

    int rc = rtt_get_board(crs, &body, &blen, &status, http_err, sizeof http_err);

    /* Access token expired/invalid — re-exchange once and retry. */
    if (rc != 0 && (status == 401 || status == 403)) {
        free(body);
        body = NULL;
        if (rtt_exchange_token(err, errsz) != 0)
            return -1;
        rc = rtt_get_board(crs, &body, &blen, &status, http_err, sizeof http_err);
    }

    if (rc != 0) {
        if (status == 401 || status == 403)
            set_err(err, errsz, "RTT rejected the token (check Settings)");
        else
            set_err(err, errsz, http_err[0] ? http_err : "fetch failed");
        free(body);
        return -1;
    }

    rc = rtt_parse(body, mode, out, err, errsz);
    free(body);
    return rc;
}
