/*
 * dbg.h — append-only diagnostic log for on-device debugging.
 *
 * Writes timestamped lines to /mnt/ext1/inkstation.log (the same file the HTTP
 * layer logs to), so the search -> lookup -> fetch flow can be traced on a real
 * reader where there is no console. Cheap and best-effort: a failed open is
 * silently ignored.
 */
#ifndef INKSTATION_DBG_H
#define INKSTATION_DBG_H

void dbg_log(const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;

#endif /* INKSTATION_DBG_H */
