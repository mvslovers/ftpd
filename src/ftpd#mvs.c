/*
** FTPD MVS Dataset Operations
**
** Catalog-based dataset access using crent370:
** - CWD: navigate dataset prefixes (quoted=absolute, unquoted=relative)
** - LIST/NLST: __listds() for datasets, __listpd() for PDS members
** - SIZE: __locate() + __dscbdv() for DSCB attributes
** - RETR/STOR/DELE/MKD/RMD/RNFR/RNTO/APPE: dataset I/O
*/
#include "ftpd.h"
#include "ftpd#ses.h"
#include "ftpd#dat.h"
#include "ftpd#mvs.h"
#include "ftpd#xlt.h"
#include "rfile.h"
#include "mvssupa.h"

/* __svc99 is OS linkage — declared under #ifdef MUSIC in mvssupa.h,
** but we need it unconditionally for dynamic allocation.
*/
#pragma linkage(__svc99, OS)
extern int __svc99(void *rb);

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
    } else if (sess->in_pds && !strchr(arg, '.') && !strchr(arg, '(')) {
        /* Inside a PDS: unquoted simple name = member reference */
        len = snprintf(buf, bufsz, "%s(%s)", sess->pds_name, arg);
        if (len >= bufsz)
            return -1;
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

    /* Leave PDS context */
    sess->in_pds = 0;
    sess->pds_name[0] = '\0';

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

    /* Track PDS context for RETR/STOR/DELE member access */
    if (is_pds == 1) {
        sess->in_pds = 1;
        strncpy(sess->pds_name, dsn, sizeof(sess->pds_name) - 1);
        sess->pds_name[sizeof(sess->pds_name) - 1] = '\0';
    } else {
        sess->in_pds = 0;
        sess->pds_name[0] = '\0';
    }

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
        ftpd_session_reply(sess, FTP_250,
            "The working directory \"%s\" is a partitioned data set",
            sess->pds_name);
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
    } else if (recfm[0] == 'U') {
        /* Load module — parse LOADDATA directly from PDS user data.
        ** __fmtloa() maps wrong fields for SIZE/TTR.
        */
        int udata_hw = pd->idc & PDSLIST_IDC_UDATA;
        if (udata_hw >= 7) {
            /* Enough user data for basic LOADDATA fields */
            LOADDATA *ld = (LOADDATA *)pd->udata;
            char attr[48];
            char *ap = attr;
            unsigned long sz;
            int ac = 0;

            /* SIZE = loadstor (3 bytes, big-endian) */
            sz = ((unsigned long)ld->loadstor[0] << 16) |
                 ((unsigned long)ld->loadstor[1] << 8) |
                  (unsigned long)ld->loadstor[2];

            /* Attributes — fixed 2-char columns matching ISPF layout:
            ** -- -- PG RF RN RU -- --  (8 positions × 3 chars)
            ** Each position is "XX " if set, "   " if absent.
            ** ISPF order: NE OL PG RF RN RU OV TS
            */
            ap = attr;
            /* NE = not executable (inverse of LOADEXEC) */
            if (!(ld->loadatr1 & LOADEXEC))
                { memcpy(ap, "NE ", 3); } else { memcpy(ap, "   ", 3); }
            ap += 3;
            /* OL = only loadable */
            if (ld->loadatr1 & LOADLOAD)
                { memcpy(ap, "OL ", 3); } else { memcpy(ap, "   ", 3); }
            ap += 3;
            /* PG = page alignment required */
            if (ld->loadftb1 & LOADPAGA)
                { memcpy(ap, "PG ", 3); } else { memcpy(ap, "   ", 3); }
            ap += 3;
            /* RF = refreshable */
            if (ld->loadatr2 & LOADREFR)
                { memcpy(ap, "RF ", 3); } else { memcpy(ap, "   ", 3); }
            ap += 3;
            /* RN = reentrant */
            if (ld->loadatr1 & LOADRENT)
                { memcpy(ap, "RN ", 3); } else { memcpy(ap, "   ", 3); }
            ap += 3;
            /* RU = reusable */
            if (ld->loadatr1 & LOADREUS)
                { memcpy(ap, "RU ", 3); } else { memcpy(ap, "   ", 3); }
            ap += 3;
            /* OV = overlay */
            if (ld->loadatr1 & LOADOVLY)
                { memcpy(ap, "OV ", 3); } else { memcpy(ap, "   ", 3); }
            ap += 3;
            /* TS = testran */
            if (ld->loadatr1 & LOADTEST)
                { memcpy(ap, "TS ", 3); } else { memcpy(ap, "   ", 3); }
            ap += 3;
            *ap = '\0';

            /* AC from APF section if present */
            if ((ld->loadftb1 & LOADAPFLG) && udata_hw >= 12) {
                LOADS04 *apf = (LOADS04 *)(pd->udata +
                    sizeof(LOADDATA));
                /* Skip scatter/alias/ssi sections based on flags */
                unsigned char *p = ld->loadbcend;
                if (ld->loadatr1 & LOADSCTR)
                    p += sizeof(LOADS01);
                if (pd->idc & PDSLIST_IDC_ALIAS)
                    p += sizeof(LOADS02);
                if (ld->loadftb1 & LOADSSI)
                    p += sizeof(LOADS03);
                apf = (LOADS04 *)p;
                if ((unsigned char *)apf + 2 <=
                    pd->udata + udata_hw * 2)
                    ac = apf->loadapfac;
            }

            /* Alias detection: if PDSLIST_IDC_ALIAS is set,
            ** the real member name is in loads02.loadmnm
            */
            {
                char aliasof[9];
                aliasof[0] = '\0';
                if (pd->idc & PDSLIST_IDC_ALIAS) {
                    /* Find the alias section in user data */
                    unsigned char *p = ld->loadbcend;
                    if (ld->loadatr1 & LOADSCTR)
                        p += sizeof(LOADS01);
                    if ((unsigned char *)p + sizeof(LOADS02) <=
                        pd->udata + udata_hw * 2) {
                        LOADS02 *al = (LOADS02 *)p;
                        memcpy(aliasof, al->loadmnm, 8);
                        aliasof[8] = '\0';
                        /* Trim trailing spaces */
                        {
                            int j = 7;
                            while (j >= 0 && aliasof[j] == ' ')
                                aliasof[j--] = '\0';
                        }
                    }
                }

                ftpd_data_printf(sess,
                    "%-8s  %06lX   %02X%02X%02X %-8s %02d %s%s    24    24\r\n",
                    name, sz,
                    pd->ttr[0], pd->ttr[1], pd->ttr[2],
                    aliasof, ac,
                    (ld->loadatr2 & LOADFLVL) ? "FO " : "   ",
                    attr);
            }
        } else {
            ftpd_data_printf(sess, "%-8s\r\n", name);
        }
    } else {
        /* Text member — use __fmtisp() */
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
        } else {
            ftpd_data_printf(sess, " %-8s\r\n", name);
        }
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

    /* Strip trailing dot from CWD for catalog queries */
    strncpy(cwd_notrail, sess->mvs_cwd, sizeof(cwd_notrail) - 1);
    cwd_notrail[sizeof(cwd_notrail) - 1] = '\0';
    cwd_len = strlen(cwd_notrail);
    if (cwd_len > 0 && cwd_notrail[cwd_len - 1] == '.')
        cwd_notrail[--cwd_len] = '\0';

    /* Use cached PDS state from CWD instead of re-checking DSCB */
    is_pds = sess->in_pds;

    member_filter = NULL;

    if (is_pds) {
        /* PDS context: arg is a member filter, not a dataset name */
        strcpy(prefix, sess->pds_name);
        if (arg && arg[0])
            member_filter = arg;
    } else if (arg && arg[0]) {
        /* Dataset context with argument.
        ** "ls *" is equivalent to "ls" (list everything under CWD).
        */
        if (strcmp(arg, "*") == 0) {
            strcpy(prefix, cwd_notrail);
        } else {
            if (resolve_dsn(sess, arg, prefix, sizeof(prefix), 1) != 0) {
                ftpd_session_reply(sess, FTP_501,
                                   "Invalid dataset name");
                return 0;
            }
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

        /* Get RECFM for formatting.
        ** Try __listds first; if it returns nothing (LISTC may not
        ** find a single exact dataset), fall back to DSCB lookup.
        */
        recfm[0] = '\0';
        dsl = __listds(prefix, "NONVSAM VOLUME", NULL);
        if (dsl && dsl[0])
            strncpy(recfm, dsl[0]->recfm, sizeof(recfm) - 1);
        if (dsl)
            __freeds(&dsl);

        if (recfm[0] == '\0') {
            /* DSCB fallback for RECFM */
            LOCWORK lw;
            DSCB dscb;
            memset(&lw, 0, sizeof(lw));
            if (__locate(prefix, &lw) == 0) {
                memset(&dscb, 0, sizeof(dscb));
                if (__dscbdv(prefix, lw.volser, &dscb) == 0) {
                    if ((dscb.dscb1.recfm & RECFU) == RECFU)
                        strcpy(recfm, "U");
                    else if (dscb.dscb1.recfm & RECFV)
                        strcpy(recfm, "V");
                    else if (dscb.dscb1.recfm & RECFF)
                        strcpy(recfm, "F");
                }
            }
        }

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
                    " Name      Size     TTR   Alias-of"
                    " AC --------- Attributes --------- Amode Rmode\r\n");
            }
        }

        for (i = 0; pds[i]; i++)
            send_pds_entry(sess, pds[i], nlst, recfm);
        __freepd(&pds);
    } else {
        /* --- Dataset listing --- */
        DSLIST **dsl;
        int has_filter;
        int has_wildcard;
        int i;
        int count;

        /* Determine if we need to filter results.
        ** "ls *" was normalized to no-arg above (prefix == cwd).
        ** Any other arg means we filter:
        **   - with wildcards: dsn_match() for pattern matching
        **   - without wildcards: prefix match on resolved name
        */
        has_filter = (arg && arg[0] && strcmp(arg, "*") != 0);
        has_wildcard = (has_filter &&
                        (strchr(arg, '*') || strchr(arg, '%')));

        dsl = __listds(cwd_notrail, "NONVSAM VOLUME", NULL);
        if (!dsl || !dsl[0]) {
            if (dsl) __freeds(&dsl);
            ftpd_session_reply(sess, FTP_550,
                               "No data sets found.");
            return 0;
        }

        /* Count matching results — if filter active, check matches */
        if (has_filter) {
            count = 0;
            for (i = 0; dsl[i]; i++) {
                if (has_wildcard) {
                    if (dsn_match(prefix, dsl[i]->dsn))
                        count++;
                } else {
                    /* Exact or prefix match for non-wildcard arg */
                    if (strcmp(prefix, dsl[i]->dsn) == 0 ||
                        (strncmp(prefix, dsl[i]->dsn,
                                 strlen(prefix)) == 0 &&
                         dsl[i]->dsn[strlen(prefix)] == '.'))
                        count++;
                }
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
            if (has_filter) {
                if (has_wildcard) {
                    if (!dsn_match(prefix, dsl[i]->dsn))
                        continue;
                } else {
                    if (strcmp(prefix, dsl[i]->dsn) != 0 &&
                        !(strncmp(prefix, dsl[i]->dsn,
                                  strlen(prefix)) == 0 &&
                          dsl[i]->dsn[strlen(prefix)] == '.'))
                        continue;
                }
            }
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

/* --------------------------------------------------------------------
** Helper: split DSN(MEMBER) into base dataset name and member name.
** If no parentheses, member[0] = '\0'.
** Modifies dsn in place (strips the member part).
** ----------------------------------------------------------------- */
static void
split_member(char *dsn, char *member, int mbrsz)
{
    char *lp = strchr(dsn, '(');
    char *rp;
    int mlen;

    member[0] = '\0';
    if (!lp)
        return;

    rp = strchr(lp, ')');
    mlen = (rp ? rp : lp + strlen(lp)) - (lp + 1);
    if (mlen >= mbrsz)
        mlen = mbrsz - 1;
    memcpy(member, lp + 1, mlen);
    member[mlen] = '\0';

    /* Remove member from dsn */
    *lp = '\0';
}

/* --------------------------------------------------------------------
** Helper: format RECFM name for 125 response.
** F/FB → "FIX", V/VB → "VAR", U → "UND"
** ----------------------------------------------------------------- */
static const char *
recfm_label(int recfm)
{
    switch (recfm) {
    case RFILE_RECFM_F: return "FIX";
    case RFILE_RECFM_V: return "VAR";
    case RFILE_RECFM_U: return "UND";
    default:            return "UNK";
    }
}

/* --------------------------------------------------------------------
** RETR — send dataset or PDS member to client
** ----------------------------------------------------------------- */
int
ftpd_mvs_retr(ftpd_session_t *sess, const char *arg)
{
    char dsn[FTPD_MAX_DSN_LEN + 2];
    char member[FTPD_MAX_MBR_LEN + 1];
    char rname[FTPD_MAX_DSN_LEN + FTPD_MAX_MBR_LEN + 4];
    RFILE *fp;
    char buf[32768];
    size_t len;
    int rc;
    long total;

    if (!arg || !arg[0]) {
        ftpd_session_reply(sess, FTP_501, "Missing dataset name");
        return 0;
    }

    rc = resolve_dsn(sess, arg, dsn, sizeof(dsn), 0);
    if (rc != 0) {
        ftpd_session_reply(sess, FTP_501, "Invalid dataset name");
        return 0;
    }

    split_member(dsn, member, sizeof(member));

    /* Build ropen() name: 'DSN' or 'DSN(MEMBER)' */
    if (member[0])
        snprintf(rname, sizeof(rname), "'%s(%s)'", dsn, member);
    else
        snprintf(rname, sizeof(rname), "'%s'", dsn);

    ftpd_log(LOG_INFO, "RETR: arg='%s' dsn='%s' member='%s' rname='%s'",
             arg, dsn, member, rname);

    rc = ropen(rname, 0, &fp);
    if (rc != 0) {
        ftpd_log(LOG_INFO, "RETR: ropen('%s') failed rc=%d", rname, rc);
        if (member[0])
            ftpd_session_reply(sess, FTP_550,
                "Request nonexistent member %s(%s) to be sent.",
                dsn, member);
        else
            ftpd_session_reply(sess, FTP_550,
                "Data set %s not found", dsn);
        return 0;
    }

    /* 125 response with RECFM and LRECL info */
    ftpd_session_reply(sess, FTP_125,
        "Sending data set %s %srecfm %d",
        member[0] ? rname + 1 : dsn,
        recfm_label(fp->recfm), fp->lrecl);

    if (ftpd_data_open(sess) != 0) {
        rclose(fp);
        ftpd_session_reply(sess, FTP_425,
                           "Cannot open data connection");
        return 0;
    }

    total = 0;

    ftpd_log(LOG_INFO, "RETR: dsn=%s type=%c lrecl=%d recfm=%d",
             dsn, sess->type == XFER_TYPE_I ? 'I' :
                  sess->type == XFER_TYPE_A ? 'A' : 'E',
             fp->lrecl, fp->recfm);

    if (sess->type == XFER_TYPE_I) {
        /* ---------------------------------------------------------------
        ** Binary mode: send raw records without translation.
        ** --------------------------------------------------------------- */
        int recnum = 0;

        ftpd_log(LOG_INFO, "RETR BIN: start lrecl=%d recfm=%d",
                 fp->lrecl, fp->recfm);

        while (rread(fp, buf, &len) == 0) {
            ftpd_data_send(sess, buf, (int)len);
            total += len;
            recnum++;
        }

        ftpd_log(LOG_INFO,
                 "RETR BIN: done sent=%ld records=%d lrecl=%d",
                 total, recnum, fp->lrecl);
    }
    else if (sess->type == XFER_TYPE_A) {
        /* ---------------------------------------------------------------
        ** ASCII mode: translate EBCDIC->ASCII, trim blanks, add CRLF.
        ** --------------------------------------------------------------- */
        while (rread(fp, buf, &len) == 0) {
            int end = (int)len;

            /* Trim trailing blanks (unless SITE TRAILING) */
            if (!sess->trailing) {
                while (end > 0 && buf[end - 1] == ' ')
                    end--;
            }

            /* Translate EBCDIC -> ASCII (CP037 for MVS datasets) */
            ftpd_xlat_mvs_e2a((unsigned char *)buf, end);

            buf[end]     = ASCII_CR;
            buf[end + 1] = ASCII_LF;
            ftpd_data_send(sess, buf, end + 2);
            total += end + 2;
        }
    }
    else {
        /* TYPE E: send EBCDIC as-is */
        while (rread(fp, buf, &len) == 0) {
            ftpd_data_send(sess, buf, (int)len);
            total += len;
        }
    }

    rclose(fp);
    ftpd_data_close(sess);

    sess->bytes_sent += total;
    sess->xfer_count++;
    if (sess->server) {
        sess->server->total_bytes_out += total;
    }

    ftpd_session_reply(sess, FTP_250,
                       "Transfer completed successfully.");
    return 0;
}

/* --------------------------------------------------------------------
** Helper: allocate a NEW dataset via SVC99 using session alloc params.
** Returns allocated ddname in ddout (8 chars + null).
** Returns 0 on success, -1 on error.
** ----------------------------------------------------------------- */
static int
alloc_new_dataset(ftpd_session_t *sess, const char *dsn,
                  int is_pds, char *ddout)
{
    /*
    ** Use the __fildef pattern from crent370 but with custom params.
    ** Build SVC99 text units directly (same as @@fildef.c).
    */
    struct rb99_local {
        char            len;
        char            verb;
        char            flag1;
        char            flag2;
        short           error;
        short           info;
        void            *txtptr;
        void            *rbx99;
        unsigned        flag3;
    } rb;

    struct tu99_local {
        short           key;
        short           numparms;
        short           parm1_len;
        char            parm1[98];
    } tu[12];

    void *tu_list[13];
    int idx = 0;
    int err;
    char spacestr[32];

    memset(&rb, 0, sizeof(rb));
    memset(tu, 0, sizeof(tu));

    /* Return DDNAME */
    tu_list[idx] = &tu[idx];
    tu[idx].key = 0x0055;       /* DALRTDDN */
    tu[idx].numparms = 1;
    tu[idx].parm1_len = 8;
    idx++;

    /* Dataset name */
    tu_list[idx] = &tu[idx];
    tu[idx].key = 0x0002;       /* DALDSNAM */
    tu[idx].numparms = 1;
    tu[idx].parm1_len = (short)strlen(dsn);
    strncpy(tu[idx].parm1, dsn, sizeof(tu[idx].parm1) - 1);
    idx++;

    /* DISP=NEW */
    tu_list[idx] = &tu[idx];
    tu[idx].key = 0x0004;       /* DALSTATS */
    tu[idx].numparms = 1;
    tu[idx].parm1_len = 1;
    tu[idx].parm1[0] = 0x04;   /* NEW */
    idx++;

    /* Normal disposition: CATALOG */
    tu_list[idx] = &tu[idx];
    tu[idx].key = 0x0005;       /* DALNDISP */
    tu[idx].numparms = 1;
    tu[idx].parm1_len = 1;
    tu[idx].parm1[0] = 0x02;   /* CATLG */
    idx++;

    /* DSORG */
    tu_list[idx] = &tu[idx];
    tu[idx].key = 0x003C;       /* DALDSORG */
    tu[idx].numparms = 1;
    tu[idx].parm1_len = 2;
    if (is_pds) {
        tu[idx].parm1[0] = 0x02;   /* PO */
        tu[idx].parm1[1] = 0x00;
    } else {
        tu[idx].parm1[0] = 0x40;   /* PS */
        tu[idx].parm1[1] = 0x00;
    }
    idx++;

    /* RECFM */
    {
        unsigned char rf = 0;
        if (sess->alloc.recfm[0] == 'F') rf = 0x80;
        else if (sess->alloc.recfm[0] == 'V') rf = 0x40;
        else if (sess->alloc.recfm[0] == 'U') rf = 0xC0;
        if (strchr(sess->alloc.recfm, 'B')) rf |= 0x10;
        if (strchr(sess->alloc.recfm, 'A')) rf |= 0x04;

        tu_list[idx] = &tu[idx];
        tu[idx].key = 0x0049;   /* DALRECFM */
        tu[idx].numparms = 1;
        tu[idx].parm1_len = 1;
        tu[idx].parm1[0] = (char)rf;
        idx++;
    }

    /* LRECL */
    if (sess->alloc.lrecl > 0) {
        tu_list[idx] = &tu[idx];
        tu[idx].key = 0x0042;   /* DALLRECL */
        tu[idx].numparms = 1;
        tu[idx].parm1_len = 2;
        tu[idx].parm1[0] = (char)((sess->alloc.lrecl >> 8) & 0xFF);
        tu[idx].parm1[1] = (char)(sess->alloc.lrecl & 0xFF);
        idx++;
    }

    /* BLKSIZE */
    if (sess->alloc.blksize > 0) {
        tu_list[idx] = &tu[idx];
        tu[idx].key = 0x0030;   /* DALBLKSZ */
        tu[idx].numparms = 1;
        tu[idx].parm1_len = 2;
        tu[idx].parm1[0] = (char)((sess->alloc.blksize >> 8) & 0xFF);
        tu[idx].parm1[1] = (char)(sess->alloc.blksize & 0xFF);
        idx++;
    }

    /* Space type: TRK or CYL */
    tu_list[idx] = &tu[idx];
    if (strcmp(sess->alloc.spacetype, "CYL") == 0) {
        tu[idx].key = 0x0008;   /* DALCYL */
    } else {
        tu[idx].key = 0x0007;   /* DALTRK */
    }
    tu[idx].numparms = 0;
    tu[idx].parm1_len = 0;
    idx++;

    /* Primary + secondary space */
    tu_list[idx] = &tu[idx];
    tu[idx].key = 0x000A;       /* DALPRIME */
    tu[idx].numparms = 1;
    tu[idx].parm1_len = 3;
    {
        int p = sess->alloc.primary > 0 ? sess->alloc.primary : 10;
        tu[idx].parm1[0] = (char)((p >> 16) & 0xFF);
        tu[idx].parm1[1] = (char)((p >> 8) & 0xFF);
        tu[idx].parm1[2] = (char)(p & 0xFF);
    }
    idx++;

    tu_list[idx] = &tu[idx];
    tu[idx].key = 0x000B;       /* DALSECND */
    tu[idx].numparms = 1;
    tu[idx].parm1_len = 3;
    {
        int s = sess->alloc.secondary > 0 ? sess->alloc.secondary : 5;
        tu[idx].parm1[0] = (char)((s >> 16) & 0xFF);
        tu[idx].parm1[1] = (char)((s >> 8) & 0xFF);
        tu[idx].parm1[2] = (char)(s & 0xFF);
    }
    idx++;

    /* Directory blocks for PDS */
    if (is_pds) {
        int d = sess->alloc.dirblks > 0 ? sess->alloc.dirblks : 10;
        tu_list[idx] = &tu[idx];
        tu[idx].key = 0x000C;   /* DALDIR */
        tu[idx].numparms = 1;
        tu[idx].parm1_len = 3;
        tu[idx].parm1[0] = (char)((d >> 16) & 0xFF);
        tu[idx].parm1[1] = (char)((d >> 8) & 0xFF);
        tu[idx].parm1[2] = (char)(d & 0xFF);
        idx++;
    }

    /* End of list */
    tu_list[idx] = (void *)0x80000000;

    /* Mark last real entry with high bit */
    {
        unsigned long last = (unsigned long)tu_list[idx - 1];
        tu_list[idx - 1] = (void *)(last | 0x80000000);
    }
    tu_list[idx] = 0;

    /* Issue SVC 99 */
    rb.len = 20;
    rb.verb = 0x01;     /* S99VRBAL = ALLOCATE */
    rb.flag1 = 0x40;    /* S99NOCNV */
    rb.txtptr = tu_list;

    err = __svc99(&rb);
    if (err) {
        ftpd_log(LOG_ERROR, "%s: SVC99 alloc failed for %s, rc=%d err=%d",
                 __func__, dsn, err, rb.error);
        return -1;
    }

    /* Return allocated DDNAME */
    memcpy(ddout, tu[0].parm1, 8);
    ddout[8] = '\0';

    return 0;
}

/* --------------------------------------------------------------------
** Helper: unallocate a ddname via SVC99.
** ----------------------------------------------------------------- */
static void
free_ddname(const char *ddname)
{
    struct rb99_local {
        char            len;
        char            verb;
        char            flag1;
        char            flag2;
        short           error;
        short           info;
        void            *txtptr;
        void            *rbx99;
        unsigned        flag3;
    } rb;

    struct tu99_local {
        short           key;
        short           numparms;
        short           parm1_len;
        char            parm1[8];
    } tu;

    void *tu_list[2];

    memset(&rb, 0, sizeof(rb));
    memset(&tu, 0, sizeof(tu));

    tu_list[0] = &tu;
    tu.key = 0x0001;             /* DALDDNAM */
    tu.numparms = 1;
    tu.parm1_len = (short)strlen(ddname);
    memcpy(tu.parm1, ddname, tu.parm1_len);

    tu_list[0] = (void *)((unsigned long)tu_list[0] | 0x80000000);
    tu_list[1] = 0;

    rb.len = 20;
    rb.verb = 0x02;              /* S99VRBUN = UNALLOCATE */
    rb.txtptr = tu_list;

    __svc99(&rb);
}

/* --------------------------------------------------------------------
** STOR — receive data from client, write to dataset/member
** ----------------------------------------------------------------- */
int
ftpd_mvs_stor(ftpd_session_t *sess, const char *arg)
{
    char dsn[FTPD_MAX_DSN_LEN + 2];
    char member[FTPD_MAX_MBR_LEN + 1];
    char rname[FTPD_MAX_DSN_LEN + FTPD_MAX_MBR_LEN + 4];
    RFILE *fp;
    LOCWORK lw;
    char ddname[9];
    int allocated_new;
    int rc;
    long total;
    char netbuf[FTPD_DATA_BUF_SIZE];
    char recbuf[32768];
    int recpos;
    int nread;

    if (!arg || !arg[0]) {
        ftpd_session_reply(sess, FTP_501, "Missing dataset name");
        return 0;
    }

    rc = resolve_dsn(sess, arg, dsn, sizeof(dsn), 0);
    if (rc != 0) {
        ftpd_session_reply(sess, FTP_501, "Invalid dataset name");
        return 0;
    }

    split_member(dsn, member, sizeof(member));
    allocated_new = 0;
    ddname[0] = '\0';

    /* Check if dataset exists */
    memset(&lw, 0, sizeof(lw));
    rc = __locate(dsn, &lw);

    if (rc != 0) {
        /* Dataset does not exist */
        if (member[0]) {
            ftpd_session_reply(sess, FTP_550,
                "%s(%s) requests a nonexistent partitioned data set. "
                "Use MKD command to create it.", dsn, member);
            return 0;
        }
        /* Allocate new dataset using SITE params */
        rc = alloc_new_dataset(sess, dsn, 0, ddname);
        if (rc != 0) {
            ftpd_session_reply(sess, FTP_550,
                "Cannot allocate dataset %s", dsn);
            return 0;
        }
        allocated_new = 1;
    }

    /* Build ropen() name */
    if (allocated_new) {
        snprintf(rname, sizeof(rname), "dd:%s", ddname);
        if (member[0]) {
            /* Should not happen (checked above) */
            snprintf(rname, sizeof(rname), "dd:%s(%s)", ddname, member);
        }
    } else if (member[0]) {
        snprintf(rname, sizeof(rname), "'%s(%s)'", dsn, member);
    } else {
        snprintf(rname, sizeof(rname), "'%s'", dsn);
    }

    rc = ropen(rname, 1, &fp);
    if (rc != 0) {
        if (allocated_new)
            free_ddname(ddname);
        ftpd_session_reply(sess, FTP_550,
            "Cannot open dataset %s for writing", dsn);
        return 0;
    }

    ftpd_session_reply(sess, FTP_125, "Storing data set %s",
                       member[0] ? arg : dsn);

    if (ftpd_data_open(sess) != 0) {
        rclose(fp);
        if (allocated_new)
            free_ddname(ddname);
        ftpd_session_reply(sess, FTP_425,
                           "Cannot open data connection");
        return 0;
    }

    total = 0;
    recpos = 0;

    ftpd_log(LOG_INFO, "STOR: dsn=%s type=%c lrecl=%d recfm=%d",
             dsn, sess->type == XFER_TYPE_I ? 'I' :
                  sess->type == XFER_TYPE_A ? 'A' : 'E',
             fp->lrecl, fp->recfm);

    if (sess->type == XFER_TYPE_I) {
        /* ---------------------------------------------------------------
        ** Binary mode: split byte stream into LRECL-sized records.
        ** Use memcpy for bulk transfer instead of byte-by-byte copy.
        ** --------------------------------------------------------------- */
        int lrecl = fp->lrecl;
        int recnum = 0;
        long total_out = 0;

        ftpd_log(LOG_INFO, "STOR BIN: start lrecl=%d recfm=%d",
                 lrecl, fp->recfm);

        while ((nread = ftpd_data_recv(sess, netbuf, sizeof(netbuf))) > 0) {
            int off = 0;
            total += nread;

            ftpd_log(LOG_DEBUG, "STOR BIN: recv=%d pending=%d total_in=%ld",
                     nread, recpos, total);

            while (off < nread) {
                int need = lrecl - recpos;
                int avail = nread - off;
                int take = (avail < need) ? avail : need;

                memcpy(recbuf + recpos, netbuf + off, take);
                recpos += take;
                off += take;

                if (recpos == lrecl) {
                    rwrite(fp, recbuf, lrecl);
                    total_out += lrecl;
                    recpos = 0;
                    recnum++;
                }
            }
        }

        /* Final partial record: pad with binary zeros */
        if (recpos > 0) {
            if (fp->recfm == RFILE_RECFM_F && lrecl > 0) {
                memset(recbuf + recpos, 0x00, lrecl - recpos);
                rwrite(fp, recbuf, lrecl);
                total_out += lrecl;
            } else {
                rwrite(fp, recbuf, recpos);
                total_out += recpos;
            }
            recnum++;
        }

        ftpd_log(LOG_INFO,
                 "STOR BIN: done recv=%ld written=%ld records=%d lrecl=%d",
                 total, total_out, recnum, lrecl);
    }
    else if (sess->type == XFER_TYPE_A) {
        /* ---------------------------------------------------------------
        ** ASCII mode: buffer until CRLF, translate, write records.
        ** --------------------------------------------------------------- */
        while ((nread = ftpd_data_recv(sess, netbuf, sizeof(netbuf))) > 0) {
            int i;
            total += nread;

            for (i = 0; i < nread; i++) {
                if (netbuf[i] == ASCII_CR) {
                    continue;   /* Skip CR, LF triggers record write */
                }
                if (netbuf[i] == ASCII_LF) {
                    /* End of record — translate ASCII→EBCDIC (CP037) */
                    ftpd_xlat_mvs_a2e((unsigned char *)recbuf, recpos);

                    /* Pad FB records with EBCDIC blanks */
                    if (fp->recfm == RFILE_RECFM_F && fp->lrecl > 0) {
                        while (recpos < fp->lrecl)
                            recbuf[recpos++] = ' ';  /* EBCDIC blank */
                        rwrite(fp, recbuf, fp->lrecl);
                    } else {
                        rwrite(fp, recbuf, recpos);
                    }
                    recpos = 0;
                    continue;
                }
                if (recpos < (int)sizeof(recbuf) - 1)
                    recbuf[recpos++] = netbuf[i];
            }
        }

        /* Flush any remaining partial record */
        if (recpos > 0) {
            ftpd_xlat_mvs_a2e((unsigned char *)recbuf, recpos);
            if (fp->recfm == RFILE_RECFM_F && fp->lrecl > 0) {
                while (recpos < fp->lrecl)
                    recbuf[recpos++] = ' ';  /* EBCDIC blank */
            }
            rwrite(fp, recbuf, recpos);
        }
    }
    else {
        /* ---------------------------------------------------------------
        ** TYPE E: write EBCDIC as-is, same record logic as binary.
        ** --------------------------------------------------------------- */
        while ((nread = ftpd_data_recv(sess, netbuf, sizeof(netbuf))) > 0) {
            int off = 0;
            total += nread;

            while (off < nread) {
                int need = fp->lrecl - recpos;
                int avail = nread - off;
                int take = (avail < need) ? avail : need;

                memcpy(recbuf + recpos, netbuf + off, take);
                recpos += take;
                off += take;

                if (recpos == fp->lrecl) {
                    rwrite(fp, recbuf, fp->lrecl);
                    recpos = 0;
                }
            }
        }

        if (recpos > 0)
            rwrite(fp, recbuf, recpos);
    }

    rclose(fp);
    ftpd_data_close(sess);

    if (allocated_new)
        free_ddname(ddname);

    sess->bytes_recv += total;
    sess->xfer_count++;
    if (sess->server) {
        sess->server->total_bytes_in += total;
    }

    ftpd_session_reply(sess, FTP_250,
                       "Transfer completed successfully.");
    return 0;
}

/* --------------------------------------------------------------------
** APPE — append to dataset (create if not exists)
** ----------------------------------------------------------------- */
int
ftpd_mvs_appe(ftpd_session_t *sess, const char *arg)
{
    /* For now, APPE behaves like STOR.
    ** True append (DISP=MOD) requires __fildef modification.
    ** ropen() with write=1 replaces content for sequential,
    ** but for PDS members it replaces the member — which is correct.
    */
    /* TODO: implement true append via DISP=MOD for sequential datasets */
    return ftpd_mvs_stor(sess, arg);
}

/* --------------------------------------------------------------------
** DELE — delete dataset or PDS member
** ----------------------------------------------------------------- */
int
ftpd_mvs_dele(ftpd_session_t *sess, const char *arg)
{
    char dsn[FTPD_MAX_DSN_LEN + 2];
    char member[FTPD_MAX_MBR_LEN + 1];
    LOCWORK lw;
    int rc;
    char cmd[128];

    if (!arg || !arg[0]) {
        ftpd_session_reply(sess, FTP_501, "Missing dataset name");
        return 0;
    }

    rc = resolve_dsn(sess, arg, dsn, sizeof(dsn), 0);
    if (rc != 0) {
        ftpd_session_reply(sess, FTP_501, "Invalid dataset name");
        return 0;
    }

    split_member(dsn, member, sizeof(member));

    /* Verify existence */
    memset(&lw, 0, sizeof(lw));
    rc = __locate(dsn, &lw);
    if (rc != 0) {
        ftpd_session_reply(sess, FTP_550,
            "DELE fails: %s does not exist.", dsn);
        return 0;
    }

    if (member[0]) {
        /* Delete PDS member via IDCAMS */
        snprintf(cmd, sizeof(cmd), " DELETE '%s(%s)'", dsn, member);
    } else {
        /* Delete entire dataset via IDCAMS */
        snprintf(cmd, sizeof(cmd), " DELETE '%s'", dsn);
    }

    rc = idcams(cmd);
    if (rc != 0) {
        ftpd_session_reply(sess, FTP_550,
            "DELE fails: %s could not be deleted.", dsn);
        return 0;
    }

    if (member[0])
        ftpd_session_reply(sess, FTP_250,
            "%s(%s) deleted.", dsn, member);
    else
        ftpd_session_reply(sess, FTP_250,
            "%s deleted.", dsn);

    return 0;
}

/* --------------------------------------------------------------------
** MKD — create a new PDS
** ----------------------------------------------------------------- */
int
ftpd_mvs_mkd(ftpd_session_t *sess, const char *arg)
{
    char dsn[FTPD_MAX_DSN_LEN + 2];
    char ddname[9];
    LOCWORK lw;
    int rc;

    if (!arg || !arg[0]) {
        ftpd_session_reply(sess, FTP_501, "Missing dataset name");
        return 0;
    }

    rc = resolve_dsn(sess, arg, dsn, sizeof(dsn), 0);
    if (rc != 0) {
        ftpd_session_reply(sess, FTP_501, "Invalid dataset name");
        return 0;
    }

    /* Check if it already exists */
    memset(&lw, 0, sizeof(lw));
    if (__locate(dsn, &lw) == 0) {
        ftpd_session_reply(sess, FTP_550,
            "%s already exists.", dsn);
        return 0;
    }

    /* Allocate new PDS */
    rc = alloc_new_dataset(sess, dsn, 1, ddname);
    if (rc != 0) {
        ftpd_session_reply(sess, FTP_550,
            "Cannot create %s", dsn);
        return 0;
    }

    /* Unallocate — we just needed to create it */
    free_ddname(ddname);

    ftpd_session_reply(sess, FTP_257,
        "'%s' created.", dsn);

    return 0;
}

/* --------------------------------------------------------------------
** RMD — remove (scratch) a PDS
** ----------------------------------------------------------------- */
int
ftpd_mvs_rmd(ftpd_session_t *sess, const char *arg)
{
    char dsn[FTPD_MAX_DSN_LEN + 2];
    LOCWORK lw;
    char cmd[128];
    int rc;

    if (!arg || !arg[0]) {
        ftpd_session_reply(sess, FTP_501, "Missing dataset name");
        return 0;
    }

    rc = resolve_dsn(sess, arg, dsn, sizeof(dsn), 0);
    if (rc != 0) {
        ftpd_session_reply(sess, FTP_501, "Invalid dataset name");
        return 0;
    }

    /* Verify existence */
    memset(&lw, 0, sizeof(lw));
    if (__locate(dsn, &lw) != 0) {
        ftpd_session_reply(sess, FTP_550,
            "\"%s\" data set does not exist.", dsn);
        return 0;
    }

    snprintf(cmd, sizeof(cmd), " DELETE '%s'", dsn);
    rc = idcams(cmd);
    if (rc != 0) {
        ftpd_session_reply(sess, FTP_550,
            "Cannot remove %s", dsn);
        return 0;
    }

    ftpd_session_reply(sess, FTP_250, "%s deleted.", dsn);

    return 0;
}

/* --------------------------------------------------------------------
** RNFR — store rename source, verify existence
** ----------------------------------------------------------------- */
int
ftpd_mvs_rnfr(ftpd_session_t *sess, const char *arg)
{
    char dsn[FTPD_MAX_DSN_LEN + 2];
    LOCWORK lw;
    int rc;

    if (!arg || !arg[0]) {
        ftpd_session_reply(sess, FTP_501, "Missing dataset name");
        return 0;
    }

    rc = resolve_dsn(sess, arg, dsn, sizeof(dsn), 0);
    if (rc != 0) {
        ftpd_session_reply(sess, FTP_501, "Invalid dataset name");
        return 0;
    }

    /* Verify existence */
    memset(&lw, 0, sizeof(lw));
    if (__locate(dsn, &lw) != 0) {
        ftpd_session_reply(sess, FTP_550,
            "RNFR fails: %s does not exist.", dsn);
        return 0;
    }

    /* Store for RNTO */
    strncpy(sess->rnfr_path, dsn, sizeof(sess->rnfr_path) - 1);
    sess->rnfr_path[sizeof(sess->rnfr_path) - 1] = '\0';

    ftpd_session_reply(sess, FTP_350,
        "RNFR accepted. Please supply new name for RNTO.");

    return 0;
}

/* --------------------------------------------------------------------
** RNTO — rename dataset from rnfr_path to new name
** ----------------------------------------------------------------- */
int
ftpd_mvs_rnto(ftpd_session_t *sess, const char *arg)
{
    char dsn[FTPD_MAX_DSN_LEN + 2];
    LOCWORK lw;
    char cmd[256];
    int rc;

    if (!sess->rnfr_path[0]) {
        ftpd_session_reply(sess, FTP_503,
            "RNFR not received.");
        return 0;
    }

    if (!arg || !arg[0]) {
        ftpd_session_reply(sess, FTP_501, "Missing dataset name");
        return 0;
    }

    rc = resolve_dsn(sess, arg, dsn, sizeof(dsn), 0);
    if (rc != 0) {
        ftpd_session_reply(sess, FTP_501, "Invalid dataset name");
        return 0;
    }

    /* Check target does not exist */
    memset(&lw, 0, sizeof(lw));
    if (__locate(dsn, &lw) == 0) {
        ftpd_session_reply(sess, FTP_550,
            "Rename fails: %s already exists.", dsn);
        sess->rnfr_path[0] = '\0';
        return 0;
    }

    /* Rename via IDCAMS ALTER */
    snprintf(cmd, sizeof(cmd),
             " ALTER '%s' NEWNAME('%s')", sess->rnfr_path, dsn);

    rc = idcams(cmd);
    if (rc != 0) {
        ftpd_session_reply(sess, FTP_550,
            "Rename of %s to %s failed.", sess->rnfr_path, dsn);
        sess->rnfr_path[0] = '\0';
        return 0;
    }

    ftpd_session_reply(sess, FTP_250,
        "%s renamed to %s", sess->rnfr_path, dsn);

    sess->rnfr_path[0] = '\0';

    return 0;
}
