/*
 * rtt.c — Realtime Trains API client (see rtt.h).
 *
 * rtt_parse() is pure (cJSON only) and host-testable. rtt_fetch() builds the
 * api.rtt.io request, sends it via the libcurl GET wrapper, and parses the
 * body. The response shape handled here is the documented one:
 *
 *   { "location": { "name": "...", "crs": "GLD" },
 *     "services": [
 *       { "locationDetail": {
 *           "gbttBookedDeparture": "1010", "realtimeDeparture": "1015",
 *           "platform": "2", "displayAs": "CALL",
 *           "destination": [ { "description": "London Waterloo" } ],
 *           "origin":      [ { "description": "..." } ] },
 *         "atocName": "South Western Railway" }, ... ] }
 *
 * Arrivals use gbttBookedArrival / realtimeArrival and the `origin` place.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "http.h"
#include "rtt.h"

static char g_cred[RTT_CRED_LEN];

void rtt_set_credential(const char *cred)
{
    if (cred && cred[0])
        snprintf(g_cred, sizeof g_cred, "%s", cred);
    else
        g_cred[0] = '\0';
}

int rtt_has_credential(void)
{
    return g_cred[0] != '\0';
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

/* Format an RTT "HHMM" time into "HH:MM". Pass-through for anything that is not
 * exactly four digits (already formatted, empty, or unexpected). */
static void fmt_time(char *dst, size_t dstsz, const char *raw)
{
    if (!raw) { dst[0] = '\0'; return; }
    if (strlen(raw) == 4 &&
        raw[0] >= '0' && raw[0] <= '9' && raw[1] >= '0' && raw[1] <= '9' &&
        raw[2] >= '0' && raw[2] <= '9' && raw[3] >= '0' && raw[3] <= '9') {
        snprintf(dst, dstsz, "%c%c:%c%c", raw[0], raw[1], raw[2], raw[3]);
    } else {
        snprintf(dst, dstsz, "%s", raw);
    }
}

/* First element's "description" from a destination/origin array. */
static void copy_place(char *dst, size_t dstsz, const cJSON *detail,
                       const char *arr_key)
{
    dst[0] = '\0';
    const cJSON *arr = cJSON_GetObjectItemCaseSensitive(detail, arr_key);
    if (!cJSON_IsArray(arr)) return;
    const cJSON *first = cJSON_GetArrayItem(arr, 0);
    copy_str(dst, dstsz, first, "description");
}

int rtt_parse(const char *json, rtt_mode mode, rtt_board *out,
              char *err, size_t errsz)
{
    if (!json || !out) { set_err(err, errsz, "no data"); return -1; }
    memset(out, 0, sizeof *out);
    out->mode = mode;

    cJSON *root = cJSON_Parse(json);
    if (!root) { set_err(err, errsz, "malformed response"); return -1; }

    const char *booked_key = (mode == RTT_DEPARTURES)
                                 ? "gbttBookedDeparture" : "gbttBookedArrival";
    const char *rt_key = (mode == RTT_DEPARTURES)
                             ? "realtimeDeparture" : "realtimeArrival";
    const char *place_key = (mode == RTT_DEPARTURES) ? "destination" : "origin";

    const cJSON *loc = cJSON_GetObjectItemCaseSensitive(root, "location");
    copy_str(out->name, sizeof out->name, loc, "name");
    copy_str(out->crs, sizeof out->crs, loc, "crs");

    const cJSON *services = cJSON_GetObjectItemCaseSensitive(root, "services");
    if (cJSON_IsArray(services)) {
        const cJSON *svc = NULL;
        cJSON_ArrayForEach(svc, services) {
            if (out->count >= RTT_MAX_SERVICES) break;

            const cJSON *detail =
                cJSON_GetObjectItemCaseSensitive(svc, "locationDetail");
            if (!detail) continue;

            const cJSON *booked =
                cJSON_GetObjectItemCaseSensitive(detail, booked_key);
            /* No relevant leg here (e.g. a terminating service in a departures
             * board) — skip it. */
            if (!cJSON_IsString(booked) || !booked->valuestring ||
                !booked->valuestring[0])
                continue;

            rtt_service *s = &out->services[out->count];

            fmt_time(s->time, sizeof s->time, booked->valuestring);
            copy_place(s->place, sizeof s->place, detail, place_key);
            copy_str(s->platform, sizeof s->platform, detail, "platform");
            copy_str(s->operator_, sizeof s->operator_, svc, "atocName");

            /* Cancellation: RTT marks it in displayAs and/or a cancel reason. */
            char display[40];
            copy_str(display, sizeof display, detail, "displayAs");
            const cJSON *cancel =
                cJSON_GetObjectItemCaseSensitive(detail, "cancelReasonCode");
            s->cancelled = (strstr(display, "CANCELLED") != NULL) ||
                           (cJSON_IsString(cancel) && cancel->valuestring &&
                            cancel->valuestring[0]);

            const cJSON *rt = cJSON_GetObjectItemCaseSensitive(detail, rt_key);
            const char *rt_val = (cJSON_IsString(rt) && rt->valuestring)
                                     ? rt->valuestring : NULL;

            if (s->cancelled) {
                snprintf(s->expected, sizeof s->expected, "Cancelled");
                s->delayed = 0;
            } else if (rt_val && rt_val[0]) {
                if (strcmp(rt_val, booked->valuestring) == 0) {
                    snprintf(s->expected, sizeof s->expected, "On time");
                    s->delayed = 0;
                } else {
                    fmt_time(s->expected, sizeof s->expected, rt_val);
                    /* Times are zero-padded HHMM, so string compare orders them
                     * correctly within a day. */
                    s->delayed = strcmp(rt_val, booked->valuestring) > 0;
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

/* Build the request URL. For Basic auth the credential is placed in the URL
 * userinfo (libcurl turns that into an Authorization: Basic header); for a
 * Bearer credential the URL carries no userinfo and *bearer points at g_cred. */
static int build_url(char *url, size_t urlsz, const char *crs, rtt_mode mode,
                     const char **bearer)
{
    *bearer = NULL;
    if (!crs || !crs[0]) return -1;

    const char *tail = (mode == RTT_ARRIVALS) ? "/arrivals" : "";
    const char *colon = strchr(g_cred, ':');

    if (colon) {
        /* Basic auth via URL userinfo. */
        if ((size_t)snprintf(url, urlsz,
                "https://%s@api.rtt.io/api/v1/json/search/%s%s",
                g_cred, crs, tail) >= urlsz)
            return -1;
    } else {
        if ((size_t)snprintf(url, urlsz,
                "https://api.rtt.io/api/v1/json/search/%s%s",
                crs, tail) >= urlsz)
            return -1;
        *bearer = g_cred;
    }
    return 0;
}

int rtt_fetch(const char *crs, rtt_mode mode, rtt_board *out,
              char *err, size_t errsz)
{
    if (!rtt_has_credential()) {
        set_err(err, errsz, "No RTT token set — see Settings");
        return -1;
    }

    char url[RTT_CRED_LEN + 128];
    const char *bearer = NULL;
    if (build_url(url, sizeof url, crs, mode, &bearer) != 0) {
        set_err(err, errsz, "bad station code");
        return -1;
    }

    char *body = NULL;
    size_t blen = 0;
    long status = 0;
    char http_err[HTTP_ERR_LEN] = {0};

    int rc = http_get(url, bearer, &body, &blen, &status, http_err,
                      sizeof http_err);
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
