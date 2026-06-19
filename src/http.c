/*
 * http.c — libcurl-backed HTTP GET for InkStation (see http.h).
 *
 * Lifted from inkshelf's http layer and trimmed to a single in-memory GET with
 * an optional Bearer header and reported HTTP status. The WiFi-reconnect retry
 * loop, the verify-off TLS posture (PocketBook firmware's libcurl is built
 * against NSS and rejects a PEM CA bundle) and the on-device diagnostic log are
 * all kept — they were hard-won on real hardware.
 */

#define _POSIX_C_SOURCE 200809L   /* localtime_r */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <curl/curl.h>

#include "http.h"
#include "net.h"

#define HTTP_UA       "inkstation/1.0 (PocketBook)"
#define HTTP_RETRIES  3            /* extra attempts on transient WiFi errors */
#define HTTP_TIMEOUT  30L
#define HTTP_MAX_BODY (4 * 1024 * 1024)   /* 4 MB cap — line-ups are tens of KB */

/* Settle delay before a retry. QueryNetwork() can report NET_CONNECTED before
 * the link is actually routable, so a retry fired the instant the previous one
 * failed just races the still-waking radio (the "connection refused after 5ms"
 * symptom). Wait this long — and re-assert the radio — before each retry so the
 * firmware has time to bring the interface fully up. */
#define HTTP_RETRY_SETTLE_MS 1500

#ifndef HTTP_LOG_PATH
#define HTTP_LOG_PATH "/mnt/ext1/inkstation.log"
#endif

static void http_log(const char *fmt, ...)
{
    FILE *lf = fopen(HTTP_LOG_PATH, "a");
    if (!lf) return;

    time_t now = time(NULL);
    struct tm tmv;
    char ts[32] = "";
    if (localtime_r(&now, &tmv))
        strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &tmv);
    fprintf(lf, "%s ", ts);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(lf, fmt, ap);
    va_end(ap);

    fputc('\n', lf);
    fclose(lf);
}

static void http_log_env_once(void)
{
    static int done = 0;
    if (done) return;
    done = 1;

    curl_version_info_data *v = curl_version_info(CURLVERSION_NOW);
    if (!v) { http_log("libcurl: version info unavailable"); return; }
    http_log("libcurl %s | ssl=%s | backend=%s",
             v->version ? v->version : "?",
             (v->features & CURL_VERSION_SSL) ? "yes" : "NO",
             v->ssl_version ? v->ssl_version : "(none)");
}

typedef struct {
    char *data;
    size_t len;
    int overflow;
} membuf;

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *ud)
{
    membuf *m = ud;
    size_t add = size * nmemb;

    if (m->len + add > HTTP_MAX_BODY) {
        m->overflow = 1;
        return 0;   /* abort transfer */
    }
    char *p = realloc(m->data, m->len + add + 1);
    if (!p) return 0;
    m->data = p;
    memcpy(m->data + m->len, ptr, add);
    m->len += add;
    m->data[m->len] = '\0';
    return add;
}

static void set_err(char *err, size_t errsz, const char *msg)
{
    if (err && errsz) {
        strncpy(err, msg, errsz - 1);
        err[errsz - 1] = '\0';
    }
}

/* See inkshelf: NSS-built libcurl rejects a PEM CA bundle, so verification is
 * turned off. InkStation only talks to the public RTT API over HTTPS; the
 * Bearer token is the only secret and the exposure (a MITM on the reader's
 * WiFi seeing train queries) is acceptable for a personal e-ink app. */
static void http_apply_tls(CURL *curl)
{
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
}

static int http_err_transient(CURLcode rc)
{
    switch (rc) {
    case CURLE_COULDNT_RESOLVE_HOST:
    case CURLE_COULDNT_CONNECT:
    case CURLE_OPERATION_TIMEDOUT:
    case CURLE_GOT_NOTHING:
    case CURLE_SEND_ERROR:
    case CURLE_RECV_ERROR:
        return 1;
    default:
        return 0;
    }
}

static void membuf_reset(membuf *m)
{
    free(m->data);
    m->data = NULL;
    m->len = 0;
    m->overflow = 0;
}

int http_get(const char *url, const char *bearer,
             char **out_buf, size_t *out_len, long *out_status,
             char *err, size_t errsz)
{
    if (!url || !out_buf || !out_len) return -1;
    *out_buf = NULL;
    *out_len = 0;
    if (out_status) *out_status = 0;

    CURL *curl = curl_easy_init();
    if (!curl) {
        set_err(err, errsz, "curl init failed");
        return -1;
    }

    membuf m = {0};
    char curlerr[CURL_ERROR_SIZE] = {0};
    struct curl_slist *headers = NULL;

    if (bearer && bearer[0]) {
        char auth[1100];
        snprintf(auth, sizeof auth, "Authorization: Bearer %s", bearer);
        headers = curl_slist_append(headers, auth);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &m);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, HTTP_UA);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, HTTP_TIMEOUT);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curlerr);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");   /* allow gzip */
    http_apply_tls(curl);

    http_log_env_once();
    http_log("GET %s (auth=%s)", url, (bearer && bearer[0]) ? "yes" : "no");

    /* WiFi-aware retry: the firmware powers the radio down on idle and
     * NetConnect() returns before the link is routable, so re-assert and wait
     * before each attempt. Only transient transport errors are retried. */
    CURLcode rc = CURLE_OK;
    int offline = 0;
    for (int attempt = 0; attempt <= HTTP_RETRIES; attempt++) {
        if (attempt > 0) {
            membuf_reset(&m);
            /* The previous attempt failed transiently — most likely it fired
             * before the radio was actually routable (QueryNetwork() reports
             * NET_CONNECTED optimistically). Re-assert the radio and wait a real
             * settle interval before retrying, rather than racing it again. */
            http_log("  prev attempt rc=%d — re-asserting WiFi, settling %dms",
                     rc, HTTP_RETRY_SETTLE_MS);
            net_ensure_online();
            struct timespec settle = {
                HTTP_RETRY_SETTLE_MS / 1000,
                (long)(HTTP_RETRY_SETTLE_MS % 1000) * 1000000L
            };
            nanosleep(&settle, NULL);
        }

        if (!net_wait_online(NET_WAIT_TIMEOUT_MS, NET_WAIT_POLL_MS)) {
            http_log("  WiFi did not reconnect within %dms — aborting",
                     NET_WAIT_TIMEOUT_MS);
            offline = 1;
            if (rc == CURLE_OK) rc = CURLE_COULDNT_CONNECT;
            break;
        }
        if (attempt > 0)
            http_log("  retry %d/%d (prev rc=%d)", attempt, HTTP_RETRIES, rc);

        rc = curl_easy_perform(curl);
        if (rc == CURLE_OK || !http_err_transient(rc))
            break;
    }

    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    if (out_status) *out_status = status;

    http_log("  -> rc=%d (%s) | http=%ld | bytes=%zu",
             rc, curl_easy_strerror(rc), status, m.len);

    if (headers) curl_slist_free_all(headers);

    if (rc != CURLE_OK) {
        if (offline)
            set_err(err, errsz, "No network connection — check WiFi");
        else if (m.overflow)
            set_err(err, errsz, "response too large");
        else {
            char cmsg[CURL_ERROR_SIZE + 32];
            snprintf(cmsg, sizeof cmsg, "curl %d: %s", rc,
                     curlerr[0] ? curlerr : curl_easy_strerror(rc));
            set_err(err, errsz, cmsg);
        }
        /* Hand the (possibly partial) body back anyway — never leak it. */
        curl_easy_cleanup(curl);
        if (!m.data) m.data = calloc(1, 1);
        *out_buf = m.data;
        *out_len = m.len;
        return -1;
    }

    curl_easy_cleanup(curl);

    if (!m.data) {
        m.data = calloc(1, 1);
        if (!m.data) { set_err(err, errsz, "out of memory"); return -1; }
    }
    *out_buf = m.data;
    *out_len = m.len;

    if (status >= 400) {
        char msg[HTTP_ERR_LEN];
        snprintf(msg, sizeof msg, "HTTP %ld from server", status);
        set_err(err, errsz, msg);
        return -1;
    }
    return 0;
}
