/*
** FTPD MVS Dataset Operations
**
** Catalog-based dataset access using crent370:
** - CWD: navigate dataset prefixes (quoted=absolute, unquoted=relative)
** - LIST/NLST: __listds() for datasets, __listpd() for PDS members
** - SIZE: __locate() + __dscbdv() for DSCB attributes
*/
#include "ftpd.h"
#include "ftpd#ses.h"
#include "ftpd#dat.h"
#include "ftpd#mvs.h"

/* --------------------------------------------------------------------
** z/OS-compatible dataset name wildcard matcher.
**
** Qualifier-based matching where each qualifier is separated by '.':
**   *   = matches exactly one qualifier (any characters)
**   **  = matches one or more qualifiers
**   %   = matches exactly one character within a qualifier
**   literal characters match case-insensitively
**
** Both pattern and name must be fully qualified (no trailing dots).
** Returns 1 if match, 0 if no match.
** ----------------------------------------------------------------- */
static int
dsn_match(const char *pattern, const char *name)
{
    const char *p = pattern;
    const char *n = name;

    while (*p && *n) {
        if (*p == '*' && *(p + 1) == '*') {
            /* ** = match one or more qualifiers */
            p += 2;
            if (*p == '.')
                p++;  /* skip dot after ** */
            if (*p == '\0')
                return 1;  /* ** at end matches everything */

            /* Try matching rest of pattern at each qualifier boundary */
            while (*n) {
                if (dsn_match(p, n))
                    return 1;
                /* Skip to next qualifier */
                while (*n && *n != '.')
                    n++;
                if (*n == '.')
                    n++;
            }
            return 0;
        }
        if (*p == '*') {
            /* * = match exactly one qualifier */
            p++;
            /* Skip the current qualifier in name */
            while (*n && *n != '.')
                n++;
            /* Both should be at dot or end */
            if (*p == '.' && *n == '.') {
                p++;
                n++;
                continue;
            }
            if (*p == '\0' && *n == '\0')
                return 1;
            return 0;
        }
        if (*p == '%') {
            /* % = match one character (not dot) */
            if (*n == '.' || *n == '\0')
                return 0;
            p++;
            n++;
            continue;
        }
        /* Literal match (case insensitive) */
        if (toupper((unsigned char)*p) != toupper((unsigned char)*n))
            return 0;
        p++;
        n++;
    }

    /* Both must be exhausted */
    return (*p == '\0' && *n == '\0');
}

/* --------------------------------------------------------------------
** Helper: build a fully-qualified dataset name from CWD + argument.
**
** Quoting rules (z/OS FTP compatible):
**   'DSN.NAME'  → absolute (strip quotes)
**   DSN.NAME    → relative: prepend mvs_cwd prefix
**
** Result is uppercased and null-terminated in buf (max 44 chars).
** Returns 0 on success, -1 if name is too long or contains wildcards.
** ----------------------------------------------------------------- */
static int
resolve_dsn(ftpd_session_t *sess, const char *arg, char *buf, int bufsz,
            int allow_wildcards)
{
    const char *src;
    int len;
    int i;

    if (!arg || !arg[0]) {
        /* No argument — use CWD as-is */
        strncpy(buf, sess->mvs_cwd, bufsz - 1);
        buf[bufsz - 1] = '\0';
        return 0;
    }

    /* Reject wildcards unless explicitly allowed (LIST/NLST) */
    if (!allow_wildcards && (strchr(arg, '*') || strchr(arg, '%'))) {
        return -2;
    }

    if (arg[0] == '\'') {
        /* Absolute: strip surrounding quotes */
        src = arg + 1;
        len = strlen(src);
        if (len > 0 && src[len - 1] == '\'')
            len--;
        if (len >= bufsz || len > FTPD_MAX_DSN_LEN)
            return -1;
        memcpy(buf, src, len);
        buf[len] = '\0';
    } else {
        /* Relative: prepend CWD prefix */
        len = snprintf(buf, bufsz, "%s%s", sess->mvs_cwd, arg);
        if (len >= bufsz || len > FTPD_MAX_DSN_LEN)
            return -1;
    }

    /* Uppercase */
    for (i = 0; buf[i]; i++)
        buf[i] = (char)toupper((unsigned char)buf[i]);

    /* Strip trailing dot if present */
    len = strlen(buf);
    if (len > 0 && buf[len - 1] == '.')
        buf[len - 1] = '\0';

    return 0;
}

/* --------------------------------------------------------------------
** CWD — change working directory (MVS dataset prefix)
** ----------------------------------------------------------------- */
/* --------------------------------------------------------------------
** Helper: generate z/OS-compatible wildcard error message.
** Identifies the problematic qualifier and says "begins with"
** or "contains" depending on position.
** ----------------------------------------------------------------- */
static void
wildcard_error(ftpd_session_t *sess, const char *arg)
{
    /* Find the qualifier containing the wildcard */
    const char *p = arg;
    const char *qstart = arg;
    char qual[46];
    const char *wc;
    int qlen;

    /* Skip leading quote */
    if (*p == '\'') {
        p++;
        qstart = p;
    }

    /* Find the qualifier with the wildcard */
    while (*p) {
        if (*p == '.' || *p == '\'') {
            /* Check if this qualifier had a wildcard */
            wc = qstart;
            while (wc < p) {
                if (*wc == '*' || *wc == '%')
                    goto found;
                wc++;
            }
            qstart = p + 1;
        }
        p++;
    }
    /* Check last qualifier */
    wc = qstart;
    while (*wc) {
        if (*wc == '*' || *wc == '%')
            goto found;
        wc++;
    }
    /* Fallback */
    ftpd_session_reply(sess, FTP_501, "Invalid dataset name");
    return;

found:
    /* Extract the qualifier */
    p = qstart;
    qlen = 0;
    while (p[qlen] && p[qlen] != '.' && p[qlen] != '\'' && qlen < 44)
        qlen++;
    memcpy(qual, p, qlen);
    qual[qlen] = '\0';

    if (qstart == arg || (arg[0] == '\'' && qstart == arg + 1) ||
        *(qstart - 1) == '.') {
        /* First char of qualifier is the wildcard? */
        if (*qstart == '*' || *qstart == '%') {
            ftpd_session_reply(sess, FTP_501,
                "A qualifier in \"%s\" begins with an invalid character",
                qual);
        } else {
            ftpd_session_reply(sess, FTP_501,
                "A qualifier in \"%s\" contains an invalid character",
                qual);
        }
    } else {
        ftpd_session_reply(sess, FTP_501,
            "A qualifier in \"%s\" contains an invalid character", qual);
    }
}

/* --------------------------------------------------------------------
** CDUP — remove last qualifier from CWD
** SYS1.MACLIB. → SYS1.
** SYS1. → (empty, reset to HLQ)
** ----------------------------------------------------------------- */
int
ftpd_mvs_cdup(ftpd_session_t *sess)
{
    char *dot;
    int len;

    /* Strip trailing dot first */
    len = strlen(sess->mvs_cwd);
    if (len > 0 && sess->mvs_cwd[len - 1] == '.')
        sess->mvs_cwd[--len] = '\0';

    /* Find the last dot — everything after it is the last qualifier */
    dot = strrchr(sess->mvs_cwd, '.');
    if (dot) {
        dot[1] = '\0';  /* Keep the dot: SYS1.MACLIB → SYS1. */
    } else {
        /* At top level — reset to HLQ */
        strcpy(sess->mvs_cwd, sess->hlq);
    }

    ftpd_session_reply(sess, FTP_250,
        "\"%s\" is the working directory name prefix.",
        sess->mvs_cwd);

    return 0;
}

/* --------------------------------------------------------------------
** CWD — change working directory (MVS dataset prefix)
**
** z/OS behavior:
** - CWD sets prefix only, no existence check (always 250)
** - Exception: if resolved name is a PDS, response says so
** - CWD .. is treated as CDUP
** ----------------------------------------------------------------- */
int
ftpd_mvs_cwd(ftpd_session_t *sess, const char *arg)
{
    char dsn[FTPD_MAX_DSN_LEN + 2];
    int rc;
    int is_pds;

    /* CWD .. / CWD handled as CDUP */
    if (arg && strcmp(arg, "..") == 0)
        return ftpd_mvs_cdup(sess);

    rc = resolve_dsn(sess, arg, dsn, sizeof(dsn), 0);
    if (rc == -2) {
        wildcard_error(sess, arg);
        return 0;
    }
    if (rc != 0) {
        ftpd_session_reply(sess, FTP_501, "Invalid dataset name");
        return 0;
    }

    /* Check if this is a PDS for the response message */
    is_pds = ftpd_mvs_is_pds(dsn);

    /* Set CWD — always add trailing dot for prefix matching */
    {
        int len = strlen(dsn);
        if (len > 0 && dsn[len - 1] != '.') {
            if (len < FTPD_MAX_DSN_LEN) {
                dsn[len] = '.';
                dsn[len + 1] = '\0';
            }
        }
    }

    strcpy(sess->mvs_cwd, dsn);

    if (is_pds == 1) {
        /* Strip trailing dot for PDS display */
        char display[FTPD_MAX_DSN_LEN + 2];
        strcpy(display, dsn);
        {
            int len = strlen(display);
            if (len > 0 && display[len - 1] == '.')
                display[len - 1] = '\0';
        }
        ftpd_session_reply(sess, FTP_250,
            "The working directory \"%s\" is a partitioned data set",
            display);
    } else {
        ftpd_session_reply(sess, FTP_250,
            "\"%s\" is the working directory name prefix.",
            sess->mvs_cwd);
    }

    ftpd_log(LOG_INFO, "%s: CWD -> %s", __func__, sess->mvs_cwd);

    return 0;
}

/* --------------------------------------------------------------------
** Check if a dataset is a PDS (DSORG=PO)
** ----------------------------------------------------------------- */
int
ftpd_mvs_is_pds(const char *dsn)
{
    LOCWORK lw;
    DSCB dscb;
    int rc;

    memset(&lw, 0, sizeof(lw));
    rc = __locate(dsn, &lw);
    if (rc != 0)
        return -1;

    memset(&dscb, 0, sizeof(dscb));
    rc = __dscbdv(dsn, lw.volser, &dscb);
    if (rc != 0)
        return -1;

    /* Check DSORG for PO (partitioned) */
    if (dscb.dscb1.dsorg1 & DSGPO)
        return 1;

    return 0;
}

/* --------------------------------------------------------------------
** Format and send dataset list entry on data connection
** ----------------------------------------------------------------- */
static void
send_ds_entry(ftpd_session_t *sess, DSLIST *ds, int nlst,
              const char *prefix)
{
    /* z/OS strips the CWD prefix from dataset names in LIST output */
    const char *name = ds->dsn;
    int pfxlen = strlen(prefix);

    if (pfxlen > 0 && strncmp(name, prefix, pfxlen) == 0)
        name += pfxlen;

    if (nlst) {
        ftpd_data_printf(sess, "%s\r\n", name);
    } else {
        ftpd_data_printf(sess,
            "%-6s %-4s %4d/%02d/%02d %2d %4d  %-5s %5d %5d  %-4s %s\r\n",
            ds->volser, ds->dev,
            ds->rfyear, ds->rfmon, ds->rfday,
            ds->extents, ds->used_trks,
            ds->recfm, ds->lrecl, ds->blksize,
            ds->dsorg, name);
    }
}

/* --------------------------------------------------------------------
** Format and send PDS member list entry
** ----------------------------------------------------------------- */
static void
send_pds_entry(ftpd_session_t *sess, PDSLIST *pd, int nlst,
               const char *recfm)
{
    char name[9];

    memcpy(name, pd->name, 8);
    name[8] = '\0';
    /* Trim trailing spaces */
    {
        int i = 7;
        while (i >= 0 && name[i] == ' ')
            name[i--] = '\0';
    }

    if (nlst) {
        ftpd_data_printf(sess, "%s\r\n", name);
    } else {
        /* Try ISPF stats for text members */
        if (recfm[0] != 'U') {
            ISPFSTAT ist;
            if (__fmtisp(pd, &ist) == 0) {
                /* Reformat dates: yy-mm-dd → YYYY/MM/DD
                ** and yy-mm-dd hh:mm:ss → YYYY/MM/DD HH:MM
                */
                char cdate[11];
                char mdate[17];
                int yy, mm, dd, hh, mi;

                if (sscanf(ist.created, "%d-%d-%d", &yy, &mm, &dd) == 3) {
                    snprintf(cdate, sizeof(cdate), "%4d/%02d/%02d",
                             yy < 70 ? 2000 + yy : 1900 + yy, mm, dd);
                } else {
                    strncpy(cdate, ist.created, sizeof(cdate) - 1);
                    cdate[sizeof(cdate) - 1] = '\0';
                }

                if (sscanf(ist.changed, "%d-%d-%d %d:%d",
                           &yy, &mm, &dd, &hh, &mi) == 5) {
                    snprintf(mdate, sizeof(mdate), "%4d/%02d/%02d %02d:%02d",
                             yy < 70 ? 2000 + yy : 1900 + yy,
                             mm, dd, hh, mi);
                } else {
                    strncpy(mdate, ist.changed, sizeof(mdate) - 1);
                    mdate[sizeof(mdate) - 1] = '\0';
                }

                ftpd_data_printf(sess,
                    "%-8s  %5s %10s %16s %5s %5s %5s %-8s\r\n",
                    ist.name, ist.ver, cdate, mdate,
                    ist.size, ist.init, ist.mod, ist.userid);
                return;
            }
        } else {
            LOADSTAT lst;
            if (__fmtloa(pd, &lst) == 0) {
                ftpd_data_printf(sess,
                    " %-8s %6s %8s  AC=%2s  %s\r\n",
                    lst.name, lst.size, lst.aliasof,
                    lst.ac, lst.attr);
                return;
            }
        }
        /* Fallback: just the name */
        ftpd_data_printf(sess, " %-8s\r\n", name);
    }
}

/* --------------------------------------------------------------------
** LIST/NLST — dataset or PDS member listing
**
** z/OS behavior:
** - In PDS context, arg is a member filter (e.g. "R*")
** - In dataset context, arg with wildcards filters the listing
** - Empty result → 550, no data connection opened
** ----------------------------------------------------------------- */
int
ftpd_mvs_list(ftpd_session_t *sess, const char *arg, int nlst)
{
    char prefix[FTPD_MAX_DSN_LEN + 2];
    char cwd_notrail[FTPD_MAX_DSN_LEN + 2];
    int is_pds;
    int cwd_len;
    const char *member_filter;

    /* Strip trailing dot from CWD for PDS check */
    strncpy(cwd_notrail, sess->mvs_cwd, sizeof(cwd_notrail) - 1);
    cwd_notrail[sizeof(cwd_notrail) - 1] = '\0';
    cwd_len = strlen(cwd_notrail);
    if (cwd_len > 0 && cwd_notrail[cwd_len - 1] == '.')
        cwd_notrail[--cwd_len] = '\0';

    /* Check if CWD is a PDS */
    is_pds = ftpd_mvs_is_pds(cwd_notrail);

    member_filter = NULL;

    if (is_pds == 1) {
        /* PDS context: arg is a member filter, not a dataset name */
        strcpy(prefix, cwd_notrail);
        if (arg && arg[0])
            member_filter = arg;
    } else if (arg && arg[0]) {
        /* Dataset context with argument: resolve as dataset pattern */
        if (resolve_dsn(sess, arg, prefix, sizeof(prefix), 1) != 0) {
            ftpd_session_reply(sess, FTP_501, "Invalid dataset name");
            return 0;
        }
    } else {
        /* No arg: use CWD prefix */
        strcpy(prefix, cwd_notrail);
    }

    if (is_pds == 1) {
        /* --- PDS member listing --- */
        PDSLIST **pds;
        DSLIST **dsl;
        char recfm[5];
        int i;
        int count;

        /* Uppercase member filter */
        char filter_buf[9];
        const char *filter = NULL;
        if (member_filter) {
            for (i = 0; i < 8 && member_filter[i]; i++)
                filter_buf[i] = (char)toupper(
                    (unsigned char)member_filter[i]);
            filter_buf[i] = '\0';
            filter = filter_buf;
        }

        pds = __listpd(prefix, filter);
        if (!pds || !pds[0]) {
            if (pds) __freepd(&pds);
            ftpd_session_reply(sess, FTP_550,
                               "No data sets found.");
            return 0;
        }

        /* Count results */
        for (count = 0; pds[count]; count++)
            ;

        /* Get RECFM for formatting */
        recfm[0] = '\0';
        dsl = __listds(prefix, "NONVSAM VOLUME", NULL);
        if (dsl && dsl[0])
            strncpy(recfm, dsl[0]->recfm, sizeof(recfm) - 1);
        if (dsl)
            __freeds(&dsl);

        /* Open data connection */
        ftpd_session_reply(sess, FTP_150,
                           "Opening data connection for file list");
        if (ftpd_data_open(sess) != 0) {
            __freepd(&pds);
            ftpd_session_reply(sess, FTP_425,
                               "Cannot open data connection");
            return 0;
        }

        /* List header */
        if (!nlst) {
            if (recfm[0] != 'U') {
                ftpd_data_printf(sess,
                    " Name     VV.MM   Created       Changed"
                    "      Size  Init   Mod   Id\r\n");
            } else {
                ftpd_data_printf(sess,
                    " Name       Size   TTR    AC  Attributes\r\n");
            }
        }

        for (i = 0; pds[i]; i++)
            send_pds_entry(sess, pds[i], nlst, recfm);
        __freepd(&pds);
    } else {
        /* --- Dataset listing --- */
        DSLIST **dsl;
        int has_wildcard;
        int i;
        int len;
        int count;

        /* Always use CWD as LISTC level (no wildcards).
        ** If arg has wildcards, filter results with dsn_match().
        */
        has_wildcard = (arg && arg[0] &&
                        (strchr(arg, '*') || strchr(arg, '%')));

        dsl = __listds(cwd_notrail, "NONVSAM VOLUME", NULL);
        if (!dsl || !dsl[0]) {
            if (dsl) __freeds(&dsl);
            ftpd_session_reply(sess, FTP_550,
                               "No data sets found.");
            return 0;
        }

        /* If wildcard filter, check if any results match */
        if (has_wildcard) {
            count = 0;
            for (i = 0; dsl[i]; i++) {
                if (dsn_match(prefix, dsl[i]->dsn))
                    count++;
            }
            if (count == 0) {
                __freeds(&dsl);
                ftpd_session_reply(sess, FTP_550,
                                   "No data sets found.");
                return 0;
            }
        }

        /* Open data connection */
        ftpd_session_reply(sess, FTP_150,
                           "Opening data connection for file list");
        if (ftpd_data_open(sess) != 0) {
            __freeds(&dsl);
            ftpd_session_reply(sess, FTP_425,
                               "Cannot open data connection");
            return 0;
        }

        /* List header (z/OS format) */
        if (!nlst) {
            ftpd_data_printf(sess,
                "Volume Unit    Referred Ext Used Recfm "
                "Lrecl BlkSz Dsorg Dsname\r\n");
        }

        for (i = 0; dsl[i]; i++) {
            if (has_wildcard && !dsn_match(prefix, dsl[i]->dsn))
                continue;
            send_ds_entry(sess, dsl[i], nlst, sess->mvs_cwd);
        }
        __freeds(&dsl);
    }

    ftpd_data_close(sess);
    ftpd_session_reply(sess, FTP_226, "List completed successfully.");

    return 0;
}

/* --------------------------------------------------------------------
** SIZE — get approximate dataset size
** ----------------------------------------------------------------- */
long
ftpd_mvs_size(ftpd_session_t *sess, const char *dsn)
{
    char name[FTPD_MAX_DSN_LEN + 2];
    LOCWORK lw;
    DSCB dscb;
    int rc;
    long size;

    if (resolve_dsn(sess, dsn, name, sizeof(name), 0) != 0)
        return -1;

    memset(&lw, 0, sizeof(lw));
    rc = __locate(name, &lw);
    if (rc != 0)
        return -1;

    memset(&dscb, 0, sizeof(dscb));
    rc = __dscbdv(name, lw.volser, &dscb);
    if (rc != 0)
        return -1;

    /* Approximate: used_tracks * blksize (rough estimate) */
    {
        unsigned short blksize = dscb.dscb1.blksz;

        if (blksize == 0)
            blksize = dscb.dscb1.lrecl;
        if (blksize == 0)
            return 0;

        /* lstar TTR: first 2 bytes = track count */
        size = ((long)dscb.dscb1.lstar[0] << 8) | dscb.dscb1.lstar[1];
        size = size * blksize;
    }

    return size;
}
