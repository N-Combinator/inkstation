/*
 * http.h — tiny HTTP GET wrapper around libcurl for InkStation.
 *
 * InkStation only ever issues small JSON GETs against the Realtime Trains API
 * (token exchange + a station line-up), so this is deliberately minimal: a
 * single blocking GET into memory, an optional Bearer token, and the HTTP
 * status reported back so the caller can tell an expired access token (401)
 * apart from a real error and refresh it. libcurl ships in the PocketBook SDK.
 */
#ifndef INKSTATION_HTTP_H
#define INKSTATION_HTTP_H

#include <stddef.h>

#define HTTP_ERR_LEN 160

/*
 * GET `url` into a freshly allocated, NUL-terminated buffer.
 *
 *   bearer      optional Bearer token for the Authorization header (NULL = none)
 *   out_buf     receives the malloc'd body (caller frees); set even on a 4xx/5xx
 *               so the caller can inspect a JSON error payload
 *   out_len     receives the body length
 *   out_status  receives the HTTP status code (0 if the request never completed)
 *
 * Returns 0 on a 2xx response. Returns -1 otherwise (transport error or
 * HTTP >= 400) and writes a message into err (if non-NULL); *out_status still
 * carries the code so the caller can special-case 401.
 */
int http_get(const char *url, const char *bearer,
             char **out_buf, size_t *out_len, long *out_status,
             char *err, size_t errsz);

#endif /* INKSTATION_HTTP_H */
