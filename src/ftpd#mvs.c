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
#include "ftpd#aut.h"
#include "ftpd#dat.h"
#include "ftpd#dsn.h"
#include "ftpd#mvs.h"
#include "ftpd#xlt.h"
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
** Helper: check RACF dataset access for the current session.
**
** Returns 0 if access is permitted, -1 if denied.
** On denial, sends a 550 reply with the dataset name.
** ----------------------------------------------------------------- */
static int
check_dataset_access(ftpd_session_t *sess, const char *dsn, int attr)
{
    int rc;

    if (!sess->acee)
        return 0;   /* no ACEE available — skip check */

    rc = racf_auth(sess->acee, "DATASET", dsn, attr);

    /* Not protected: no profile covers this data set.  OPEN, IDCAMS and the
    ** catalog all proceed on that answer, so refusing here would make FTPD
    ** stricter than every other path to the same data set on the system. */
    if (rc == FTPD_RACF_NOTPROT)
        ftpd_log(LOG_DEBUG, "%s: %s is not protected by RAKF — allowed for %s",
                 __func__, dsn, sess->user);

    if (!ftpd_racf_allowed(rc)) {
        ftpd_log(LOG_WARN, "%s: %s access denied to %s for %s",
                 __func__, attr == RACF_ATTR_READ    ? "READ"    :
                           attr == RACF_ATTR_UPDATE  ? "UPDATE"  :
                           attr == RACF_ATTR_CONTROL ? "CONTROL" :
                           "ALTER",
                 dsn, sess->user);
        ftpd_session_reply(sess, FTP_550,
            "Access denied to %s", dsn);
        return -1;
    }
    return 0;
}

/* --------------------------------------------------------------------
** Helper: sanitize a client-supplied PDS member name into a valid MVS
** member name:
**   - strip any extension (everything from the first '.')
**   - uppercase
**   - truncate to 8 characters (FTPD_MAX_MBR_LEN)
** Writes at most FTPD_MAX_MBR_LEN + 1 bytes to out.
** Returns 0 on success, -1 if no usable name remains (e.g. ".foo").
** ----------------------------------------------------------------- */
static int
sanitize_member(const char *in, char *out, int outsz)
{
    int m = 0;
    int i;

    if (outsz < 1)
        return -1;

    for (i = 0; in[i] && in[i] != '.' &&
                m < FTPD_MAX_MBR_LEN && m < outsz - 1; i++)
        out[m++] = (char)toupper((unsigned char)in[i]);
    out[m] = '\0';

    return (m > 0) ? 0 : -1;
}

/* --------------------------------------------------------------------
** Helper: build a fully-qualified dataset name from CWD + argument.
**
** Quoting rules (z/OS FTP compatible):
**   'DSN.NAME'  → absolute (strip quotes)
**   DSN.NAME    → relative: prepend mvs_cwd prefix
**
** Result is uppercased and null-terminated in buf (max 44 chars).
**
** Returns 0 on success, or, with the reply left to dsn_error():
**   -1  the name is too long
**   -2  the name holds a wildcard and the caller resolves one name
**   -3  the name holds a character a data set name may not hold
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

    /* Reject wildcards unless explicitly allowed (LIST/NLST).  Asked of the
    ** raw argument rather than of the resolved name, because the PDS member
    ** branch below returns before the name is validated -- and a member is
    ** not a pattern either. */
    if (!allow_wildcards && strpbrk(arg, FTPD_DSN_WILDCARDS) != NULL) {
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
    } else if (sess->in_pds && arg[0] != '\'' && !strchr(arg, '(')) {
        /* Inside a PDS: an unquoted argument is a MEMBER reference.
        ** A dot here is a filename extension, not a qualifier separator.
        ** Strip any leading path, then sanitize the filename into a
        ** valid member (extension stripped, uppercased, max 8 chars).
        ** Arguments that already contain '(' are an explicit member
        ** form and fall through to the branch below, where split_member()
        ** applies the same sanitization. */
        char member[FTPD_MAX_MBR_LEN + 1];
        const char *base = arg;
        const char *slash;

        slash = strrchr(base, '/');
        if (slash) base = slash + 1;

        if (sanitize_member(base, member, sizeof(member)) != 0)
            return -1;   /* no usable member name, e.g. ".foo" */

        len = snprintf(buf, bufsz, "%s(%s)", sess->pds_name, member);
        if (len >= bufsz)
            return -1;

        return 0;        /* member already clean; skip uppercase/dot-strip */
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

    /* Ask last, with the name in the form the rest of FTPD will use it:
    ** a client that sends its local path as the remote name (`put
    ** build/x.xmit`) used to get that '/' carried through LOCATE and into
    ** SVC 99, which reported an allocation failure for a name that was
    ** never allocatable (#95). */
    if (!ftpd_dsn_valid(buf, allow_wildcards))
        return -3;

    return 0;
}

/* --------------------------------------------------------------------
** CWD — change working directory (MVS dataset prefix)
** ----------------------------------------------------------------- */
/* --------------------------------------------------------------------
** Helper: is c one of the characters that make a name a pattern?
** Guards against '\0', which strchr() would report as a member of any set.
** ----------------------------------------------------------------- */
static int
is_wildcard(char c)
{
    return c != '\0' && strchr(FTPD_DSN_WILDCARDS, c) != NULL;
}

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
                if (is_wildcard(*wc))
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
        if (is_wildcard(*wc))
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
        if (is_wildcard(*qstart)) {
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
** Helper: reply to a resolve_dsn() failure.
**
** Every command resolves a name the same way and used to answer the same
** single "Invalid dataset name" for every way it can go wrong.  Saying
** which one it was is worth more than the sentence: a wildcard names the
** qualifier that carries it, and an unusable character comes back with the
** name FTPD actually built, which is the surprising part when the client
** sent a path and got a data set name (#95).
**
** arg -- the argument as the client sent it, for the wildcard message.
** dsn -- the resolved name; read only for -3, where resolve_dsn() has
**        filled it in completely.
** ----------------------------------------------------------------- */
static void
dsn_error(ftpd_session_t *sess, const char *arg, const char *dsn, int rc)
{
    if (rc == -2) {
        wildcard_error(sess, arg);
        return;
    }

    if (rc == -3) {
        ftpd_session_reply(sess, FTP_501,
            "Invalid data set name \"'%s'\".  Use MVS Dsname conventions.",
            dsn);
        return;
    }

    ftpd_session_reply(sess, FTP_501, "Invalid dataset name");
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
    int has_trailing_dot;

    /* CWD .. / CWD handled as CDUP */
    if (arg && strcmp(arg, "..") == 0)
        return ftpd_mvs_cdup(sess);

    /* Detect trailing dot BEFORE resolve_dsn strips it.
    ** z/OS behavior (verified on z/OS 3.1):
    **   CWD 'SYS1.MACLIB'  → PDS detection via OBTAIN (no trailing dot)
    **   CWD 'SYS1.MACLIB.' → prefix-only, no I/O (trailing dot) */
    has_trailing_dot = 0;
    if (arg && arg[0]) {
        int alen = strlen(arg);
        /* Check the char before closing quote (quoted) or last char (unquoted) */
        if (arg[0] == '\'' && alen >= 3 && arg[alen - 1] == '\'') {
            has_trailing_dot = (arg[alen - 2] == '.');
        } else {
            has_trailing_dot = (arg[alen - 1] == '.');
        }
    }

    rc = resolve_dsn(sess, arg, dsn, sizeof(dsn), 0);
    if (rc != 0) {
        dsn_error(sess, arg, dsn, rc);
        return 0;
    }

    /* PDS detection only if argument had NO trailing dot.
    ** Trailing dot = prefix-only mode (no I/O, no OBTAIN). */
    if (!has_trailing_dot) {
        is_pds = ftpd_mvs_is_pds(dsn);
    } else {
        is_pds = 0;
    }

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

    ftpd_log(LOG_INFO, "%s: CWD -> %s (in_pds=%d trailing_dot=%d)",
             __func__, sess->mvs_cwd, sess->in_pds, has_trailing_dot);

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
** Decode a packed BCD byte to integer (e.g. 0x19 → 19).
** ----------------------------------------------------------------- */
static int
bcd_byte(unsigned char b)
{
    return ((b >> 4) & 0x0F) * 10 + (b & 0x0F);
}

/* --------------------------------------------------------------------
** Convert day-of-year to month and day-of-month.
** ----------------------------------------------------------------- */
static void
julian_to_cal(int year, int doy, int *mon, int *mday)
{
    static const int mdays[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int leap = (year % 4 == 0);
    int m, d;

    d = doy;
    for (m = 0; m < 12; m++) {
        int md = mdays[m];
        if (m == 1 && leap) md++;
        if (d <= md) break;
        d -= md;
    }
    *mon = m + 1;
    *mday = (d > 0) ? d : 1;
}

/* --------------------------------------------------------------------
** Check if a PDS member has valid ISPF statistics in its userdata.
** Returns 1 if the userdata looks like ISPF format, 0 otherwise.
** ----------------------------------------------------------------- */
static int
has_valid_ispf_stats(PDSLIST *pd)
{
    int udata_hw;
    unsigned char ver, mod;

    udata_hw = pd->idc & PDSLIST_IDC_UDATA;

    /* Minimum 7 halfwords (14 bytes) covers the core ISPF fields
    ** through modhm[2].  Full stats with userid are 15 hw (30 bytes),
    ** but older ISPF versions and some utilities store fewer.
    */
    if (udata_hw < 7) return 0;

    /* Version: binary 1-99 (NOT packed BCD despite cliblist.h comment) */
    ver = pd->udata[0];
    if (ver == 0 || ver > 99) return 0;

    /* Mod: binary 0-99 */
    mod = pd->udata[1];
    if (mod > 99) return 0;

    /* Century bytes (offset 4 and 8): must be 0 (1900s) or 1 (2000s).
    ** Only check if we have enough bytes.
    */
    if (udata_hw >= 3 && pd->udata[4] > 1) return 0;  /* crecent */
    if (udata_hw >= 5 && pd->udata[8] > 1) return 0;  /* modcent */

    return 1;
}

/* --------------------------------------------------------------------
** Format ISPF statistics directly from the raw ispfdata struct.
** Bypasses __fmtisp() which has stricter halfword requirements.
** Sends the formatted line on the data connection.
** ----------------------------------------------------------------- */
static void
send_ispf_stats(ftpd_session_t *sess, PDSLIST *pd, const char *name)
{
    ISPFDATA *isp = (ISPFDATA *)pd->udata;
    int udata_hw = pd->idc & PDSLIST_IDC_UDATA;
    int vv, mm;
    int cyy, cddd, myy, mddd;
    int cyear, myear;
    int cmon, cmday, mmon, mmday;
    int hh, mi;
    unsigned curlines, initlines, modlines;
    char userid[9];

    /* Version and mod are binary (not packed BCD) */
    vv = isp->ver;
    mm = isp->mod;

    /* Decode created julian date: 3 bytes packed = YYDDD+ */
    cyy = bcd_byte(isp->creydd[0]);
    cddd = ((isp->creydd[1] >> 4) & 0x0F) * 100
         + (isp->creydd[1] & 0x0F) * 10
         + ((isp->creydd[2] >> 4) & 0x0F);
    cyear = (isp->crecent == 0) ? 1900 + cyy : 2000 + cyy;
    julian_to_cal(cyear, cddd, &cmon, &cmday);

    /* Decode modified julian date */
    myy = bcd_byte(isp->modydd[0]);
    mddd = ((isp->modydd[1] >> 4) & 0x0F) * 100
         + (isp->modydd[1] & 0x0F) * 10
         + ((isp->modydd[2] >> 4) & 0x0F);
    myear = (isp->modcent == 0) ? 1900 + myy : 2000 + myy;
    julian_to_cal(myear, mddd, &mmon, &mmday);

    /* Decode packed time */
    hh = bcd_byte(isp->modhm[0]);
    mi = bcd_byte(isp->modhm[1]);

    /* Line counts (need at least 10 hw = 20 bytes) */
    if (udata_hw >= 10) {
        curlines = isp->curlines;
        initlines = isp->initlines;
        modlines = isp->modlines;
    } else {
        curlines = initlines = modlines = 0;
    }

    /* Userid (need at least 14 hw = 28 bytes) */
    if (udata_hw >= 14) {
        memcpy(userid, isp->userid, 8);
        userid[8] = '\0';
        {
            int j = 7;
            while (j >= 0 && userid[j] == ' ')
                userid[j--] = '\0';
        }
    } else {
        userid[0] = '\0';
    }

    ftpd_data_printf(sess,
        "%-8s  %2d.%02d %4d/%02d/%02d %4d/%02d/%02d %02d:%02d"
        " %5u %5u %5u %-8s\r\n",
        name, vv, mm,
        cyear, cmon, cmday,
        myear, mmon, mmday, hh, mi,
        curlines, initlines, modlines, userid);
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
        /* Text member — decode ISPF stats directly from raw ispfdata.
        ** Bypasses __fmtisp() which rejects members with < 15 halfwords.
        ** System utilities (IEBUPDTE, IEBCOPY) write non-ISPF userdata;
        ** has_valid_ispf_stats() filters those out.
        */
        if (has_valid_ispf_stats(pd)) {
            send_ispf_stats(sess, pd, name);
        } else {
            ftpd_data_printf(sess, "%-8s\r\n", name);
        }
    }
}

/* --------------------------------------------------------------------
** LIST/NLST — dataset or PDS member listing
**
** z/OS behavior:
** - In PDS context, arg is a member filter (e.g. "R*")
** - In dataset context, arg with wildcards filters the listing
** - Empty dataset result → empty listing with header (z/OS compat)
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
            int rc = resolve_dsn(sess, arg, prefix, sizeof(prefix), 1);
            if (rc != 0) {
                dsn_error(sess, arg, prefix, rc);
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

        /* Authorize READ against the logged-in user before opening the
        ** PDS directory. __listpd() OPENs the PDS with BPAM; without this
        ** the OPEN runs under the STC identity (FTPD), so a RAKF denial
        ** escalates to ABEND S913. Mirrors the RETR/STOR access check. */
        if (check_dataset_access(sess, prefix, RACF_ATTR_READ) != 0)
            return 0;   /* 550 already sent — do NOT open the PDS */

        if (member_filter) {
            for (i = 0; i < 8 && member_filter[i]; i++)
                filter_buf[i] = (char)toupper(
                    (unsigned char)member_filter[i]);
            filter_buf[i] = '\0';
            filter = filter_buf;
        }

        /* Open the PDS directory under the user's security environment,
        ** so the RAKF authorization check evaluates against the user. */
        ftpd_acee_enter(sess);
        pds = __listpd(prefix, filter);
        ftpd_acee_leave(sess);
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

        /* Determine if we need to filter results.
        ** "ls *" was normalized to no-arg above (prefix == cwd).
        ** Any other arg means we filter:
        **   - with wildcards: dsn_match() for pattern matching
        **   - without wildcards: prefix match on resolved name
        */
        has_filter = (arg && arg[0] && strcmp(arg, "*") != 0);
        has_wildcard = (has_filter &&
                        (strchr(arg, '*') || strchr(arg, '%')));

        /* __listds() is a catalog/VTOC operation (LISTCAT-style): it reads
        ** catalog entries for the prefix with no dataset OPEN, so unlike
        ** the __listpd() OPEN above it should neither ABEND on a protected
        ** prefix nor need a user-ACEE switch -- confirm on TK5. A prefix
        ** such as "SYS1." spans many datasets and is not a single RACF
        ** DATASET resource, so there is no per-dataset READ check here. */
        dsl = __listds(cwd_notrail, "NONVSAM VOLUME", NULL);

        /* __listds() returns NULL both for "prefix not cataloged" and
        ** "prefix exists but has no children".  Use __locate() to tell
        ** them apart: if the prefix itself is cataloged, the level is
        ** valid → return empty listing with header (z/OS behavior).
        ** If __locate() also fails → prefix doesn't exist → 550.
        */
        if (!dsl || !dsl[0]) {
            LOCWORK lw;
            if (dsl) __freeds(&dsl);
            memset(&lw, 0, sizeof(lw));
            if (__locate(cwd_notrail, &lw) != 0) {
                ftpd_session_reply(sess, FTP_550,
                                   "No data sets found.");
                return 0;
            }
            dsl = NULL;  /* valid prefix, empty result */
        }

        /* Open data connection */
        ftpd_session_reply(sess, FTP_150,
                           "Opening data connection for file list");
        if (ftpd_data_open(sess) != 0) {
            if (dsl) __freeds(&dsl);
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

        if (dsl) {
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
    }

    ftpd_data_close(sess);
    ftpd_session_reply(sess, FTP_226, "List completed successfully.");

    return 0;
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
    char raw[FTPD_MAX_DSN_LEN + 1];
    int rlen;

    member[0] = '\0';
    if (!lp)
        return;

    /* Extract the raw text between the parentheses */
    rp = strchr(lp, ')');
    rlen = (rp ? rp : lp + strlen(lp)) - (lp + 1);
    if (rlen >= (int)sizeof(raw))
        rlen = sizeof(raw) - 1;
    memcpy(raw, lp + 1, rlen);
    raw[rlen] = '\0';

    /* Sanitize: strip extension, uppercase, max 8 chars. A client may
    ** send 'DSN(name.ext)' (e.g. FileZilla) — without this the extension
    ** and its dot would leak into the physical member name. */
    if (sanitize_member(raw, member, mbrsz) != 0)
        member[0] = '\0';

    /* Remove member part from dsn */
    *lp = '\0';
}

/* --------------------------------------------------------------------
** Helper: what an idcams() return code means, in words.
**
** The value is IDCAMS's highest condition code.  The messages that explain
** it go to SYSPRINT, and libc370's IDCAMS IO exit discards them
** (src/clib/idcams.c, OP_PUT) -- so the condition code is all a caller has,
** and "failed" is all the client used to be told.
**
** Measured on MVS 3.8j, one IDCAMS step per case:
**   ALTER of a data set that does not exist  IDC3012I ENTRY ... NOT FOUND
**                                            -> condition code 8
**   ALTER to a name with a 9-character qualifier
**                                            IDC3203I ITEM ... DOES NOT
**                                            ADHERE TO RESTRICTIONS -> 12
**   DELETE of a data set that does not exist IDC3012I ... NOT FOUND -> 8
**
** 8 is the catalog's general "no" and covers a refusal as well as a missing
** entry, so the wording names both rather than guessing between them.
** Negative values come from libc370: IDCAMS could not be invoked at all.
** ----------------------------------------------------------------- */
static const char *
idcams_reason(int rc)
{
    if (rc < 0)
        return "IDCAMS could not be invoked";

    switch (rc) {
    case 4:  return "IDCAMS reported a warning";
    case 8:  return "not in the catalog, or refused";
    case 12: return "the data set name was rejected as invalid";
    case 16: return "IDCAMS terminated";
    default: return "IDCAMS reported an error";
    }
}

/* --------------------------------------------------------------------
** Helper: is 'member' present in PDS 'dsn'?
**
** __locate() answers for the base data set only — it resolves DSN(MEMBER) by
** ignoring the member entirely — so member existence needs the directory.
** Reads it with __listpd() under the session identity, exactly as LIST does;
** the caller has already established that the base data set exists.
**
** Returns 1 if the member is there, 0 if it is not (or the directory could
** not be read, which for the callers here means the same thing: do not
** proceed).
** ----------------------------------------------------------------- */
static int
member_exists(ftpd_session_t *sess, const char *dsn, const char *member)
{
    PDSLIST **pds;
    char want[8];
    int found = 0;
    int i;

    /* Directory entries are 8 bytes, blank padded and not terminated. */
    memset(want, ' ', sizeof(want));
    for (i = 0; i < (int)sizeof(want) && member[i]; i++)
        want[i] = member[i];

    ftpd_acee_enter(sess);
    pds = __listpd(dsn, member);
    ftpd_acee_leave(sess);

    if (!pds)
        return 0;

    for (i = 0; pds[i]; i++) {
        if (memcmp(pds[i]->name, want, sizeof(want)) == 0) {
            found = 1;
            break;
        }
    }

    __freepd(&pds);
    return found;
}

/* --------------------------------------------------------------------
** Helper: format RECFM name for 125 response.
** Uses _FILE_RECFM_* constants from clibio.h (FILE struct recfm).
** F/FB → "FIX", V/VB → "VAR", U → "UND"
** ----------------------------------------------------------------- */
static const char *
recfm_label(unsigned char recfm)
{
    switch (recfm & _FILE_RECFM_TYPE) {
    case _FILE_RECFM_F: return "FIX";
    case _FILE_RECFM_V: return "VAR";
    case _FILE_RECFM_U: return "UND";
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
    char fname[FTPD_MAX_DSN_LEN + FTPD_MAX_MBR_LEN + 4];
    FILE *fp;
    char buf[32768];
    size_t n;
    int rc;
    long total;
    int lrecl;
    int is_fixed;

    if (!arg || !arg[0]) {
        ftpd_session_reply(sess, FTP_501, "Missing dataset name");
        return 0;
    }

    rc = resolve_dsn(sess, arg, dsn, sizeof(dsn), 0);
    if (rc != 0) {
        dsn_error(sess, arg, dsn, rc);
        return 0;
    }

    split_member(dsn, member, sizeof(member));

    /* Build fopen filename — single-quoted for fully qualified DSN */
    if (member[0])
        snprintf(fname, sizeof(fname), "'%s(%s)'", dsn, member);
    else
        snprintf(fname, sizeof(fname), "'%s'", dsn);

    ftpd_log(LOG_INFO, "RETR: arg='%s' dsn='%s' member='%s' fname='%s'",
             arg, dsn, member, fname);

    /* RACF access check */
    if (check_dataset_access(sess, dsn, RACF_ATTR_READ) != 0)
        return 0;

    /* Switch to user's security environment for fopen */
    ftpd_acee_enter(sess);
    fp = fopen(fname, "rb");
    ftpd_acee_leave(sess);
    if (fp == NULL) {
        ftpd_log(LOG_INFO, "RETR: fopen('%s') failed", fname);
        if (member[0])
            ftpd_session_reply(sess, FTP_550,
                "Request nonexistent member %s(%s) to be sent.",
                dsn, member);
        else
            ftpd_session_reply(sess, FTP_550,
                "Data set %s not found", dsn);
        return 0;
    }

    /* Track the open handle so ABEND recovery can release it (DCB +
    ** fopen's dynalloc DD) if the transfer below ABENDs. */
    sess->cur_file = fp;

    /* Read LRECL/RECFM from file handle — crent370 populates from DCB.
    ** For RECFM=U, lrecl is 0; use blksize instead (mvsmf pattern). */
    is_fixed = (fp->recfm & _FILE_RECFM_TYPE) == _FILE_RECFM_F;
    {
        int is_undefined =
            (fp->recfm & _FILE_RECFM_TYPE) == _FILE_RECFM_U;
        lrecl = is_undefined ? (int)fp->blksize : (int)fp->lrecl;
    }

    /* 125 response with RECFM and LRECL info */
    ftpd_session_reply(sess, FTP_125,
        "Sending data set %s %srecfm %d",
        member[0] ? arg : dsn,
        recfm_label(fp->recfm), lrecl);

    if (ftpd_data_open(sess) != 0) {
        sess->cur_file = NULL;
        fclose(fp);
        ftpd_session_reply(sess, FTP_425,
                           "Cannot open data connection");
        return 0;
    }

    total = 0;

#ifdef FTPD_DEBUG_ABEND
    /* SITE ABEND=XFER injection (#63): ABEND here with cur_file set and the
    ** data connection open, to exercise recovery's fclose(cur_file) +
    ** clear-before-close idempotency.  One-shot: disarm before firing. */
    if (sess->debug_abend_xfer) {
        volatile int *trap = (volatile int *)0;
        sess->debug_abend_xfer = 0;
        ftpd_log_wto("FTPD072W DEBUG ABEND=XFER firing mid-RETR socket=%d",
                     sess->ctrl_sock);
        *trap = 0;                     /* force S0C4 */
    }
#endif

    ftpd_log(LOG_INFO, "RETR: dsn=%s type=%c lrecl=%d blksize=%d recfm=0x%02X",
             dsn, sess->type == XFER_TYPE_I ? 'I' :
                  sess->type == XFER_TYPE_A ? 'A' : 'E',
             lrecl, fp->blksize, fp->recfm);

    if (sess->type == XFER_TYPE_I) {
        /* ---------------------------------------------------------------
        ** Binary mode: fread lrecl bytes per record, send raw.
        ** Matches mvsmf dsapi.c read_and_send_dataset(DATA_TYPE_BINARY).
        ** --------------------------------------------------------------- */
        int recnum = 0;

        if (lrecl > 0) {
            while ((n = fread(buf, 1, lrecl, fp)) > 0) {
                ftpd_data_send(sess, buf, (int)n);
                total += n;
                recnum++;
            }
        }

        ftpd_log(LOG_INFO,
                 "RETR BIN: done sent=%ld records=%d lrecl=%d",
                 total, recnum, lrecl);
    }
    else if (sess->type == XFER_TYPE_A) {
        /* ---------------------------------------------------------------
        ** ASCII mode: fread lrecl bytes per record, trim, xlat, CRLF.
        ** For FB: each fread returns exactly LRECL bytes (one record).
        ** --------------------------------------------------------------- */
        if (lrecl > 0) {
            while ((n = fread(buf, 1, lrecl, fp)) > 0) {
                int end = (int)n;

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
    }
    else {
        /* TYPE E: send EBCDIC as-is, fread lrecl bytes per record */
        if (lrecl > 0) {
            while ((n = fread(buf, 1, lrecl, fp)) > 0) {
                ftpd_data_send(sess, buf, (int)n);
                total += n;
            }
        }
    }

    ftpd_log(LOG_INFO, "RETR: closing %s", fname);
    sess->cur_file = NULL;
    fclose(fp);
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
    char fname[FTPD_MAX_DSN_LEN + FTPD_MAX_MBR_LEN + 4];
    FILE *fp;
    LOCWORK lw;
    char ddname[9];
    int ds_exists;
    int allocated_new;
    int rc;
    long total;
    char netbuf[FTPD_DATA_BUF_SIZE];
    char recbuf[32768];
    int recpos;
    int nread;
    int is_fixed;

    if (!arg || !arg[0]) {
        ftpd_session_reply(sess, FTP_501, "Missing dataset name");
        return 0;
    }

    rc = resolve_dsn(sess, arg, dsn, sizeof(dsn), 0);
    if (rc != 0) {
        dsn_error(sess, arg, dsn, rc);
        return 0;
    }

    split_member(dsn, member, sizeof(member));
    allocated_new = 0;
    ddname[0] = '\0';

    /* RACF access check */
    if (check_dataset_access(sess, dsn, RACF_ATTR_UPDATE) != 0)
        return 0;

    /* Check if dataset exists */
    memset(&lw, 0, sizeof(lw));
    ds_exists = (__locate(dsn, &lw) == 0);

    if (!ds_exists && member[0]) {
        ftpd_session_reply(sess, FTP_550,
            "%s(%s) requests a nonexistent partitioned data set. "
            "Use MKD command to create it.", dsn, member);
        return 0;
    }

    /* Step 1: If dataset doesn't exist, create it via __dsalcf().
    ** This is the proven mvsMF DSAPI pattern (dsapi.c line 1794):
    ** __dsalcf() for SVC 99 allocation, __dsfree() to release DD,
    ** then fopen("wb") reads DCB from the new DSCB on DASD. */
    if (!ds_exists) {
        char opts[256];
        snprintf(opts, sizeof(opts),
            "DSN=%s;DISP=(NEW,CATLG,DELETE);DSORG=PS;RECFM=%s;"
            "LRECL=%d;BLKSIZE=%d;SPACE=%s(%d,%d)",
            dsn,
            sess->alloc.recfm,
            sess->alloc.lrecl,
            sess->alloc.blksize,
            sess->alloc.spacetype[0] ? sess->alloc.spacetype : "TRK",
            sess->alloc.primary   > 0 ? sess->alloc.primary   : 100,
            sess->alloc.secondary > 0 ? sess->alloc.secondary : 50);

        ftpd_log(LOG_INFO, "STOR: __dsalcf opts='%s'", opts);

        /* Allocate under the logged-in user's security environment, not
        ** the STC identity — the subsequent fopen()/OPEN runs under the
        ** same ACEE. Running SVC 99 under FTPD would authorize against
        ** the STC, then OPEN under the user ACEE would ABEND S130.
        **
        ** NOTE (ABEND-recovery residual): this __dsalcf/__dsfree pair
        ** completes before cur_file is set, so an ABEND in this narrow
        ** window is NOT caught by recovery's fclose(cur_file).  It would
        ** leak the DD and, because DISP=(NEW,CATLG,DELETE) catalogs on
        ** normal disposition, leave an empty catalogued dataset behind.
        ** The window is a few instructions (two SVC 99 calls) and is not
        ** instrumented; total_recover in the operator log makes any such
        ** accumulation observable. */
        ftpd_acee_enter(sess);
        rc = __dsalcf(ddname, "%s", opts);
        if (rc == 0)
            __dsfree(ddname);   /* release DD — fopen will re-allocate */
        ftpd_acee_leave(sess);

        if (rc != 0) {
            ftpd_log(LOG_ERROR, "STOR: __dsalcf failed rc=%d", rc);
            ftpd_session_reply(sess, FTP_550,
                "Cannot allocate dataset %s", dsn);
            return 0;
        }
        allocated_new = 1;
    }

    /* Step 2: Build fopen filename — single-quoted for fully qualified DSN.
    ** Always open with just "wb" — crent370 reads DCB from DSCB. */
    if (member[0])
        snprintf(fname, sizeof(fname), "'%s(%s)'", dsn, member);
    else
        snprintf(fname, sizeof(fname), "'%s'", dsn);

    ftpd_log(LOG_INFO, "STOR: fopen('%s', 'wb') new=%d", fname, allocated_new);

    /* Switch to user's security environment for fopen */
    ftpd_acee_enter(sess);
    fp = fopen(fname, "wb");
    ftpd_acee_leave(sess);
    if (fp == NULL) {
        ftpd_session_reply(sess, FTP_550,
            "Cannot open dataset %s for writing", dsn);
        return 0;
    }

    /* Track the open handle so ABEND recovery can release it (DCB +
    ** fopen's dynalloc DD) if the transfer below ABENDs. */
    sess->cur_file = fp;

    ftpd_session_reply(sess, FTP_125, "Storing data set %s",
                       member[0] ? arg : dsn);

    if (ftpd_data_open(sess) != 0) {
        sess->cur_file = NULL;
        fclose(fp);
        ftpd_session_reply(sess, FTP_425,
                           "Cannot open data connection");
        return 0;
    }

    total = 0;
    recpos = 0;
    is_fixed = (fp->recfm & _FILE_RECFM_TYPE) == _FILE_RECFM_F;

    ftpd_log(LOG_INFO, "STOR: dsn=%s type=%c lrecl=%d blksize=%d recfm=0x%02X",
             dsn, sess->type == XFER_TYPE_I ? 'I' :
                  sess->type == XFER_TYPE_A ? 'A' : 'E',
             fp->lrecl, fp->blksize, fp->recfm);

    if (sess->type == XFER_TYPE_I) {
        /* ---------------------------------------------------------------
        ** Binary mode: exact mvsMF dsapi.c pattern (lines 1013-1054).
        ** Accumulate into eff_lrecl-sized records, fwrite+fflush each.
        ** recv directly into record_buffer, limited to space remaining.
        ** --------------------------------------------------------------- */
        int is_undefined =
            (fp->recfm & _FILE_RECFM_TYPE) == _FILE_RECFM_U;
        size_t eff_lrecl = is_undefined
            ? (size_t)fp->blksize : (size_t)fp->lrecl;
        char *record_buffer;
        size_t record_pos = 0;
        int recnum = 0;

        if (eff_lrecl == 0) {
            sess->cur_file = NULL;
            fclose(fp);
            ftpd_data_close(sess);
            ftpd_session_reply(sess, FTP_550,
                "Dataset has zero record length");
            return 0;
        }

        record_buffer = calloc(1, eff_lrecl);
        if (!record_buffer) {
            sess->cur_file = NULL;
            fclose(fp);
            ftpd_data_close(sess);
            ftpd_session_reply(sess, FTP_550,
                "Memory allocation failed");
            return 0;
        }

        ftpd_log(LOG_INFO,
                 "STOR BIN: start eff_lrecl=%lu recfm=0x%02X%s",
                 (unsigned long)eff_lrecl, fp->recfm,
                 is_undefined ? " (U)" : "");

        /* Recv directly into record_buffer, limited to space left */
        while (1) {
            size_t space = eff_lrecl - record_pos;
            int n = ftpd_data_recv(sess,
                        record_buffer + record_pos, (int)space);
            if (n <= 0) break;

            total += n;
            record_pos += n;

            if (record_pos >= eff_lrecl) {
                fwrite(record_buffer, 1, record_pos, fp);
                fflush(fp);
                record_pos = 0;
                recnum++;
            }
        }

        /* Final partial record: pad with 0x00 (skip for RECFM=U) */
        if (record_pos > 0) {
            if (!is_undefined) {
                memset(record_buffer + record_pos, 0x00,
                       eff_lrecl - record_pos);
                record_pos = eff_lrecl;
            }
            fwrite(record_buffer, 1, record_pos, fp);
            fflush(fp);
            recnum++;
        }

        free(record_buffer);

        ftpd_log(LOG_INFO,
                 "STOR BIN: done total=%ld records=%d eff_lrecl=%lu",
                 total, recnum, (unsigned long)eff_lrecl);
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
                    if (is_fixed && fp->lrecl > 0) {
                        while (recpos < fp->lrecl)
                            recbuf[recpos++] = ' ';  /* EBCDIC blank */
                        fwrite(recbuf, 1, fp->lrecl, fp);
                        fflush(fp);
                    } else {
                        fwrite(recbuf, 1, recpos, fp);
                        fflush(fp);
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
            if (is_fixed && fp->lrecl > 0) {
                while (recpos < fp->lrecl)
                    recbuf[recpos++] = ' ';  /* EBCDIC blank */
            }
            fwrite(recbuf, 1, recpos, fp);
            fflush(fp);
        }
    }
    else {
        /* ---------------------------------------------------------------
        ** TYPE E: EBCDIC as-is, same record accumulation as binary.
        ** --------------------------------------------------------------- */
        int is_undefined =
            (fp->recfm & _FILE_RECFM_TYPE) == _FILE_RECFM_U;
        size_t eff_lrecl = is_undefined
            ? (size_t)fp->blksize : (size_t)fp->lrecl;
        char *record_buffer;
        size_t record_pos = 0;

        if (eff_lrecl > 0) {
            record_buffer = calloc(1, eff_lrecl);
        } else {
            record_buffer = NULL;
        }

        if (record_buffer) {
            while (1) {
                size_t space = eff_lrecl - record_pos;
                int n = ftpd_data_recv(sess,
                            record_buffer + record_pos, (int)space);
                if (n <= 0) break;
                total += n;
                record_pos += n;

                if (record_pos >= eff_lrecl) {
                    fwrite(record_buffer, 1, record_pos, fp);
                    fflush(fp);
                    record_pos = 0;
                }
            }
            if (record_pos > 0) {
                if (!is_undefined) {
                    memset(record_buffer + record_pos, 0x00,
                           eff_lrecl - record_pos);
                    record_pos = eff_lrecl;
                }
                fwrite(record_buffer, 1, record_pos, fp);
                fflush(fp);
            }
            free(record_buffer);
        }
    }

    ftpd_log(LOG_INFO, "STOR: closing %s", fname);
    sess->cur_file = NULL;
    fclose(fp);
    ftpd_data_close(sess);

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
    ** True append (DISP=MOD) requires fopen mode change.
    ** fopen("wb") replaces content for sequential datasets,
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
        dsn_error(sess, arg, dsn, rc);
        return 0;
    }

    split_member(dsn, member, sizeof(member));

    /* RACF access check */
    if (check_dataset_access(sess, dsn, RACF_ATTR_ALTER) != 0)
        return 0;

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

    /* Switch to user's security environment for IDCAMS */
    ftpd_acee_enter(sess);
    rc = idcams(cmd);
    ftpd_acee_leave(sess);
    if (rc != 0) {
        ftpd_log(LOG_ERROR, "%s: idcams(\"%s\") rc=%d", __func__, cmd, rc);
        ftpd_session_reply(sess, FTP_550,
            "DELE fails: %s could not be deleted -- %s (IDCAMS rc=%d).",
            dsn, idcams_reason(rc), rc);
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
        dsn_error(sess, arg, dsn, rc);
        return 0;
    }

    /* RACF access check */
    if (check_dataset_access(sess, dsn, RACF_ATTR_ALTER) != 0)
        return 0;

    /* Check if it already exists */
    memset(&lw, 0, sizeof(lw));
    if (__locate(dsn, &lw) == 0) {
        ftpd_session_reply(sess, FTP_550,
            "%s already exists.", dsn);
        return 0;
    }

    /* Allocate new PDS under user's security environment */
    ftpd_acee_enter(sess);
    rc = alloc_new_dataset(sess, dsn, 1, ddname);
    ftpd_acee_leave(sess);
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
        dsn_error(sess, arg, dsn, rc);
        return 0;
    }

    /* RACF access check */
    if (check_dataset_access(sess, dsn, RACF_ATTR_ALTER) != 0)
        return 0;

    /* Verify existence */
    memset(&lw, 0, sizeof(lw));
    if (__locate(dsn, &lw) != 0) {
        ftpd_session_reply(sess, FTP_550,
            "\"%s\" data set does not exist.", dsn);
        return 0;
    }

    snprintf(cmd, sizeof(cmd), " DELETE '%s'", dsn);

    /* Switch to user's security environment for IDCAMS */
    ftpd_acee_enter(sess);
    rc = idcams(cmd);
    ftpd_acee_leave(sess);
    if (rc != 0) {
        ftpd_log(LOG_ERROR, "%s: idcams(\"%s\") rc=%d", __func__, cmd, rc);
        ftpd_session_reply(sess, FTP_550,
            "Cannot remove %s -- %s (IDCAMS rc=%d).",
            dsn, idcams_reason(rc), rc);
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
    char full[FTPD_MAX_DSN_LEN + 2];
    char member[FTPD_MAX_MBR_LEN + 1];
    LOCWORK lw;
    int rc;

    if (!arg || !arg[0]) {
        ftpd_session_reply(sess, FTP_501, "Missing dataset name");
        return 0;
    }

    rc = resolve_dsn(sess, arg, dsn, sizeof(dsn), 0);
    if (rc != 0) {
        dsn_error(sess, arg, dsn, rc);
        return 0;
    }

    /* Keep the name as given for RNTO, then work on the base name.  Every
    ** check below is about the data set or its directory, and neither
    ** __locate() nor a RAKF DATASET profile can be asked about DSN(MEMBER):
    ** LOCATE resolves the base name and ignores the member, and profiles are
    ** held under data set names (#92). */
    strncpy(full, dsn, sizeof(full) - 1);
    full[sizeof(full) - 1] = '\0';
    split_member(dsn, member, sizeof(member));

    /* RACF access check (ALTER needed for rename) */
    if (check_dataset_access(sess, dsn, RACF_ATTR_ALTER) != 0)
        return 0;

    /* Verify existence — of the data set, and of the member if one is named */
    memset(&lw, 0, sizeof(lw));
    if (__locate(dsn, &lw) != 0) {
        ftpd_session_reply(sess, FTP_550,
            "RNFR fails: %s does not exist.", dsn);
        return 0;
    }

    if (member[0] && !member_exists(sess, dsn, member)) {
        ftpd_session_reply(sess, FTP_550,
            "RNFR fails: %s(%s) does not exist.", dsn, member);
        return 0;
    }

    /* Store for RNTO */
    strncpy(sess->rnfr_path, full, sizeof(sess->rnfr_path) - 1);
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
    char tmember[FTPD_MAX_MBR_LEN + 1];
    char src[FTPD_MAX_DSN_LEN + 2];
    char smember[FTPD_MAX_MBR_LEN + 1];
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
        dsn_error(sess, arg, dsn, rc);
        return 0;
    }

    strncpy(src, sess->rnfr_path, sizeof(src) - 1);
    src[sizeof(src) - 1] = '\0';
    split_member(src, smember, sizeof(smember));
    split_member(dsn, tmember, sizeof(tmember));

    /* A member and a data set are different kinds of thing: STOW renames a
    ** directory entry, IDCAMS ALTER renames a catalogued data set, and
    ** neither turns one into the other.  Say which way round it was. */
    if ((smember[0] != '\0') != (tmember[0] != '\0')) {
        if (smember[0])
            ftpd_session_reply(sess, FTP_550,
                "Rename fails: cannot rename member %s(%s) to data set %s.",
                src, smember, dsn);
        else
            ftpd_session_reply(sess, FTP_550,
                "Rename fails: cannot rename data set %s to member %s(%s).",
                src, dsn, tmember);
        sess->rnfr_path[0] = '\0';
        return 0;
    }

    /* --- member -> member: STOW change, not IDCAMS --------------------- */
    if (smember[0]) {
        if (strcmp(src, dsn) != 0) {
            ftpd_session_reply(sess, FTP_550,
                "Rename fails: cannot move a member between data sets "
                "(%s -> %s).", src, dsn);
            sess->rnfr_path[0] = '\0';
            return 0;
        }

        if (check_dataset_access(sess, dsn, RACF_ATTR_ALTER) != 0) {
            sess->rnfr_path[0] = '\0';
            return 0;
        }

        /* __renmem() allocates, opens, STOWs and closes the PDS, so it
        ** carries its own existence checks — rc 8 for a missing source and
        ** rc 4 for an occupied target are exactly the two answers __locate()
        ** could never give here (#92). */
        ftpd_acee_enter(sess);
        rc = __renmem(dsn, smember, tmember);
        ftpd_acee_leave(sess);

        switch (rc) {
        case 0:
            ftpd_session_reply(sess, FTP_250, "%s(%s) renamed to %s(%s)",
                               dsn, smember, dsn, tmember);
            break;
        case 4:
            ftpd_session_reply(sess, FTP_550,
                "Rename fails: %s(%s) already exists.", dsn, tmember);
            break;
        case 8:
            ftpd_session_reply(sess, FTP_550,
                "Rename fails: %s(%s) does not exist.", dsn, smember);
            break;
        default:
            ftpd_log(LOG_ERROR, "%s: __renmem(%s,%s,%s) rc=%d",
                     __func__, dsn, smember, tmember, rc);
            ftpd_session_reply(sess, FTP_550,
                "Rename of %s(%s) to %s(%s) failed.",
                dsn, smember, dsn, tmember);
            break;
        }

        sess->rnfr_path[0] = '\0';
        return 0;
    }

    /* --- data set -> data set: IDCAMS ALTER ---------------------------- */

    /* Check target does not exist */
    memset(&lw, 0, sizeof(lw));
    if (__locate(dsn, &lw) == 0) {
        ftpd_session_reply(sess, FTP_550,
            "Rename fails: %s already exists.", dsn);
        sess->rnfr_path[0] = '\0';
        return 0;
    }

    /* Authorize the TARGET name too.  RNFR already checked ALTER on the
    ** source; without this the new name is only ever checked by IDCAMS
    ** itself, i.e. by the RAKF check that runs under the address-space-wide
    ** ASXBSENV — the one identity FTPD cannot guarantee is its own while
    ** other sessions run (#64).  A denial clears the pending RNFR, like
    ** every other failure path here. */
    if (check_dataset_access(sess, dsn, RACF_ATTR_ALTER) != 0) {
        sess->rnfr_path[0] = '\0';
        return 0;
    }

    /* Rename via IDCAMS ALTER under user's security environment */
    snprintf(cmd, sizeof(cmd),
             " ALTER '%s' NEWNAME('%s')", src, dsn);

    ftpd_acee_enter(sess);
    rc = idcams(cmd);
    ftpd_acee_leave(sess);
    if (rc != 0) {
        ftpd_log(LOG_ERROR, "%s: idcams(\"%s\") rc=%d", __func__, cmd, rc);
        ftpd_session_reply(sess, FTP_550,
            "Rename of %s to %s failed -- %s (IDCAMS rc=%d).",
            src, dsn, idcams_reason(rc), rc);
        sess->rnfr_path[0] = '\0';
        return 0;
    }

    ftpd_session_reply(sess, FTP_250,
        "%s renamed to %s", src, dsn);

    sess->rnfr_path[0] = '\0';

    return 0;
}
