/*
 * dbg.c — see dbg.h.
 */
#define _POSIX_C_SOURCE 200809L

#include <stdarg.h>
#include <stdio.h>
#include <time.h>

#include "dbg.h"

#ifndef DBG_LOG_PATH
#define DBG_LOG_PATH "/mnt/ext1/inkstation.log"
#endif

void dbg_log(const char *fmt, ...)
{
    FILE *lf = fopen(DBG_LOG_PATH, "a");
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
