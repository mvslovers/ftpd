/*
** FTPD spool file table of LIST JOBnnnnn
**
** See include/ftpd#jsl.h.
*/
#include <stdio.h>
#include <string.h>

#include "ftpd#jsl.h"

#define JSL_BYTES_MAX 2147483647L

static int
jsl_done(int n, unsigned bufsz)
{
    return (n < 0 || (unsigned)n >= bufsz) ? -1 : n;
}

int
ftpd_jsl_rule(char *buf, unsigned bufsz)
{
    return jsl_done(snprintf(buf, bufsz, "%s", "--------"), bufsz);
}

int
ftpd_jsl_head(char *buf, unsigned bufsz)
{
    return jsl_done(snprintf(buf, bufsz, "%s",
                    "        ID  STEPNAME PROCSTEP C DDNAME   BYTE-COUNT "),
                    bufsz);
}

int
ftpd_jsl_row(char *buf, unsigned bufsz, int id, const char *step,
             const char *proc, char cls, const char *ddname, long bytes,
             int jes2)
{
    char c = cls ? cls : ' ';

    if (!step) step = "";
    if (!proc) proc = "";
    if (!ddname) ddname = "";
    if (!step[0] && jes2) step = "JES2";
    if (id < 1 || id > FTPD_JSL_MAXID) return -1;
    if (bytes < 0) bytes = 0;
    if (bytes > JSL_BYTES_MAX) bytes = JSL_BYTES_MAX;

    return jsl_done(snprintf(buf, bufsz,
                    "        %03d %-8.8s %-8.8s %c %-8.8s %10ld",
                    id, step, proc, c, ddname, bytes), bufsz);
}

int
ftpd_jsl_tail(char *buf, unsigned bufsz, int count)
{
    return jsl_done(snprintf(buf, bufsz, "%d spool files", count), bufsz);
}
