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

#include "clibgrt.h"
#include "clibwto.h"
#include "ftpd#log.h"

/* --- Log level names --- */
static const char *level_names[] = { "ERROR", "WARN ", "INFO ", "DEBUG" };

/* --- Trace ring buffer --- */
#define TRACE_ENTRY_LEN     120     /* max length of one trace entry  */

/* ====================================================================
** Log/trace state
**
** All of it used to be file-scope variables here.  In an AC(1) module
** fetched from an APF-authorized library that storage is key 0 and a
** store from the key-8 STC abends S0C4 (#101), so the state now lives in
** ftpd_server -- a main() local -- and is reached through grtapp2.
**
** The GRT is per process and inherited by every subtask (@@CRTSET copies
** crtgrt from the mother task's CLIBCRT), so a session worker resolves
** the same block main published.  A NULL slot means main has not got
** there yet: the callers below fall back to LOG_INFO and no tracing
** rather than to a wild store.
** ================================================================= */
static ftpd_logst_t *
logst(void)
{
    CLIBGRT *grt = __grtget();

    return grt ? (ftpd_logst_t *)grt->grtapp2 : NULL;
}

void
ftpd_log_anchor(ftpd_logst_t *st)
{
    CLIBGRT *grt = __grtget();

    if (grt)
        grt->grtapp2 = st;
}

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
    ftpd_logst_t *st = logst();

    if (level > (st ? st->level : LOG_INFO))
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
    ftpd_logst_t *st = logst();

    if (st && level >= LOG_ERROR && level <= LOG_DEBUG)
        st->level = level;
}

int
ftpd_log_get_level(void)
{
    ftpd_logst_t *st = logst();

    return st ? st->level : LOG_INFO;
}

/* ====================================================================
** Trace ring buffer
** ================================================================= */
int
ftpd_trace_init(int size)
{
    ftpd_logst_t *st = logst();

    if (!st)
        return -1;

    if (size < 1)
        size = 256;

    st->trace_buf = calloc(size, TRACE_ENTRY_LEN);
    if (!st->trace_buf)
        return -1;

    st->trace_size = size;
    st->trace_head = 0;
    st->trace_count = 0;
    st->trace_on = 0;

    return 0;
}

void
ftpd_trace_free(void)
{
    ftpd_logst_t *st = logst();

    if (!st)
        return;

    if (st->trace_buf) {
        free(st->trace_buf);
        st->trace_buf = NULL;
    }
    st->trace_size = 0;
    st->trace_head = 0;
    st->trace_count = 0;
    st->trace_on = 0;
}

void
ftpd_trace(const char *fmt, ...)
{
    char *entry;
    va_list ap;
    ftpd_logst_t *st = logst();

    if (!st || !st->trace_on || !st->trace_buf)
        return;

    entry = st->trace_buf + (st->trace_head * TRACE_ENTRY_LEN);

    va_start(ap, fmt);
    vsnprintf(entry, TRACE_ENTRY_LEN, fmt, ap);
    va_end(ap);

    st->trace_head = (st->trace_head + 1) % st->trace_size;
    st->trace_count++;
}

void
ftpd_trace_enable(int on)
{
    ftpd_logst_t *st = logst();

    if (!st)
        return;

    /* Allocate first, enable second.  ftpd_trace_init() resets trace_on,
    ** so the old order left TRACE ON reporting FTPD080I with tracing
    ** still off -- unreachable today because initialize() allocates the
    ** ring at startup, but wrong the moment that stops being true. */
    if (on && !st->trace_buf)
        ftpd_trace_init(256);

    st->trace_on = on;
}

int
ftpd_trace_dump(void)
{
    int i, idx, count;
    ftpd_logst_t *st = logst();

    if (!st || !st->trace_buf) {
        printf("Trace buffer not initialized\n");
        return 0;
    }

    count = (st->trace_count < st->trace_size) ? st->trace_count
                                               : st->trace_size;
    if (count == 0) {
        printf("Trace buffer empty\n");
        return 0;
    }

    /* Start from oldest entry */
    if (st->trace_count >= st->trace_size) {
        idx = st->trace_head;  /* oldest is at head (next overwritten) */
    } else {
        idx = 0;               /* buffer not yet wrapped */
    }

    printf("--- Trace dump: %d entries (%d total written) ---\n",
           count, st->trace_count);

    for (i = 0; i < count; i++) {
        char *entry = st->trace_buf + (idx * TRACE_ENTRY_LEN);
        if (entry[0] != '\0') {
            printf("[%04d] %s\n", i, entry);
        }
        idx = (idx + 1) % st->trace_size;
    }

    printf("--- End of trace dump ---\n");

    return count;
}

int
ftpd_trace_enabled(void)
{
    ftpd_logst_t *st = logst();

    return st ? st->trace_on : 0;
}
