/*
** FTPD Logging
**
** Three output channels:
**   1. WTO (Write To Operator) -- important events only
**   2. STDOUT -- general logging with timestamp and level
**   3. Trace ring buffer -- diagnostic capture, enabled via console
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

#include "clibwto.h"
#include "ftpd#log.h"

/* --- Log level names --- */
static const char *level_names[] = { "ERROR", "WARN ", "INFO ", "DEBUG" };

/* --- Current log level (default INFO) --- */
static int log_level = LOG_INFO;

/* --- Trace ring buffer --- */
#define TRACE_ENTRY_LEN     120     /* max length of one trace entry  */

static char     *trace_buf = NULL;  /* ring buffer storage            */
static int      trace_size = 0;     /* number of entries              */
static int      trace_head = 0;     /* next write position            */
static int      trace_count = 0;    /* total entries written          */
static int      trace_on = 0;       /* tracing enabled flag           */

/* ====================================================================
** WTO logging
** ================================================================= */
void
ftpd_log_wto(const char *fmt, ...)
{
    char buf[128];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    wto(buf);
}

/* ====================================================================
** Upper case a string into a caller-supplied buffer
**
** The console house style is upper case; only values keep their original
** case.  Everything that reaches a WTO as a literal is already written in
** upper case -- this is for the strings that arrive lower case at runtime:
** the build stamp (MBT_VERSION, MBT_COMMIT) and libc370_version().
** ================================================================= */
const char *
ftpd_upcase(char *dst, unsigned n, const char *src)
{
    unsigned i;

    if (n == 0) return dst;

    for (i = 0; i + 1U < n && src[i]; i++)
        dst[i] = (char)toupper((unsigned char)src[i]);
    dst[i] = '\0';

    return dst;
}

/* ====================================================================
** General logging to STDOUT
** ================================================================= */
void
ftpd_log(int level, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    const char *lvl;

    if (level > log_level)
        return;

    lvl = (level >= 0 && level <= LOG_DEBUG) ? level_names[level] : "?????";

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    printf("[%s] %s\n", lvl, buf);
}

void
ftpd_log_set_level(int level)
{
    if (level >= LOG_ERROR && level <= LOG_DEBUG)
        log_level = level;
}

int
ftpd_log_get_level(void)
{
    return log_level;
}

/* ====================================================================
** Trace ring buffer
** ================================================================= */
int
ftpd_trace_init(int size)
{
    if (size < 1)
        size = 256;

    trace_buf = calloc(size, TRACE_ENTRY_LEN);
    if (!trace_buf)
        return -1;

    trace_size = size;
    trace_head = 0;
    trace_count = 0;
    trace_on = 0;

    return 0;
}

void
ftpd_trace_free(void)
{
    if (trace_buf) {
        free(trace_buf);
        trace_buf = NULL;
    }
    trace_size = 0;
    trace_head = 0;
    trace_count = 0;
    trace_on = 0;
}

void
ftpd_trace(const char *fmt, ...)
{
    char *entry;
    va_list ap;

    if (!trace_on || !trace_buf)
        return;

    entry = trace_buf + (trace_head * TRACE_ENTRY_LEN);

    va_start(ap, fmt);
    vsnprintf(entry, TRACE_ENTRY_LEN, fmt, ap);
    va_end(ap);

    trace_head = (trace_head + 1) % trace_size;
    trace_count++;
}

void
ftpd_trace_enable(int on)
{
    trace_on = on;
    if (on && !trace_buf) {
        ftpd_trace_init(256);
    }
}

int
ftpd_trace_dump(void)
{
    int i, idx, count;

    if (!trace_buf) {
        printf("Trace buffer not initialized\n");
        return 0;
    }

    count = (trace_count < trace_size) ? trace_count : trace_size;
    if (count == 0) {
        printf("Trace buffer empty\n");
        return 0;
    }

    /* Start from oldest entry */
    if (trace_count >= trace_size) {
        idx = trace_head;  /* oldest is at head (about to be overwritten) */
    } else {
        idx = 0;           /* buffer not yet wrapped */
    }

    printf("--- Trace dump: %d entries (%d total written) ---\n",
           count, trace_count);

    for (i = 0; i < count; i++) {
        char *entry = trace_buf + (idx * TRACE_ENTRY_LEN);
        if (entry[0] != '\0') {
            printf("[%04d] %s\n", i, entry);
        }
        idx = (idx + 1) % trace_size;
    }

    printf("--- End of trace dump ---\n");

    return count;
}

int
ftpd_trace_enabled(void)
{
    return trace_on;
}
