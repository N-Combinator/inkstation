/*
 * config.h — tiny key=value settings store for InkStation.
 *
 * Persists settings (currently just the Realtime Trains refresh token and the
 * last-viewed station) in a flat "key=value" file on the device, by default
 *   /mnt/ext1/system/config/inkstation.conf
 *
 * Deliberately minimal: no sections, no escaping — keys are short identifiers
 * and values are single-line strings. Writes are atomic (temp + rename) so a
 * yanked SD card never leaves a half-written config behind.
 *
 * The RTT refresh token is a secret: it lives only in this on-device file,
 * never in the repo or the binary. See README "Configuration".
 */

#ifndef INKSTATION_CONFIG_H
#define INKSTATION_CONFIG_H

#include <stddef.h>

#define CONFIG_VALUE_MAX 1024   /* RTT refresh tokens are long */

/* Well-known keys. */
#define CONFIG_KEY_RTT_TOKEN  "rtt_refresh_token"
#define CONFIG_KEY_LAST_CRS   "last_crs"

/*
 * Look up `key`. On success copies the value into `out` (always NUL-terminated)
 * and returns 0; returns -1 if the file or key is missing (out set to "").
 */
int config_get(const char *key, char *out, size_t outsz);

/*
 * Set `key` to `value`, preserving all other keys. Creates the config file (and
 * its directory) if needed. Returns 0 on success, -1 on I/O failure.
 */
int config_set(const char *key, const char *value);

/*
 * Override the config file path (used by host unit tests). Pass NULL to reset
 * to the built-in device default. The string is copied.
 */
void config_set_path(const char *path);

#endif /* INKSTATION_CONFIG_H */
