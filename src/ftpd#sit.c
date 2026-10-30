/*
** FTPD SITE Command Processing
**
** z/OS-compatible SITE subcommands for allocation parameters,
** transfer modifiers, and JES interface settings.
**
** Recognized parameters return "200 SITE command was accepted" and take
** effect; a parameter FTPD cannot honour (RDW, a SBSENDEOL other than CRLF)
** returns 501 instead of being accepted and ignored (#207).
** A keyword may be abbreviated as far as z/OS allows (PRI=, SEC=, TR, CY,
** REC=, LR=, BLK=, DIR=, ...); see site_keywords[].
** Unknown parameters return "200-Unrecognized parameter ... / 200 SITE ...".
** A RETPD above MAXRETPD returns 501 naming the limit (#201).
** Bare SITE (no args) returns "202 SITE not necessary; you may proceed".
*/
#include "ftpd.h"
#include "ftpd#ses.h"
#include "ftpd#dsn.h"
#include "ftpd#mvs.h"
#include "ftpd#sit.h"
#include "ftpd#jfl.h"
#include "ftpd#jes.h"
#ifdef FTPD_DEBUG_ABEND
#include "ftpd#aut.h"            /* ftpd_acee_enter() — ABEND=LOCK  */
#endif

/* --------------------------------------------------------------------
** Helper: parse "KEY=VALUE" from token.
** Sets *key and *val to point into work buffer (null-terminated).
** If no '=', val is empty string.
** Returns 0 on success.
** ----------------------------------------------------------------- */
static int
parse_kv(const char *arg, char *key, int keysz, char *val, int valsz)
{
    const char *eq;
    int klen, vlen;

    while (*arg == ' ') arg++;

    eq = strchr(arg, '=');
    if (eq) {
        klen = eq - arg;
        if (klen >= keysz) klen = keysz - 1;
        memcpy(key, arg, klen);
        key[klen] = '\0';

        eq++;
        vlen = strlen(eq);
        if (vlen >= valsz) vlen = valsz - 1;
        memcpy(val, eq, vlen);
        val[vlen] = '\0';
    } else {
        klen = strlen(arg);
        if (klen >= keysz) klen = keysz - 1;
        memcpy(key, arg, klen);
        key[klen] = '\0';
        val[0] = '\0';
    }

    /* Uppercase key */
    {
        int i;
        for (i = 0; key[i]; i++)
            key[i] = (char)toupper((unsigned char)key[i]);
    }

    return 0;
}

/* --------------------------------------------------------------------
** SITE keywords and their minimum abbreviations.
**
** min is the shortest accepted abbreviation.  PRI, SEC, TR, CY, RECF, LREC,
** BLK and DIR are the forms z/OS scripts are known to send (#130); the other
** lengths are the shortest ones that are unique among the keywords FTPD has.
** DIRECTORY and UNIT take a single letter (D=, U=), as the z/OS SITE syntax
** diagram writes them; DCBDSN needs two (DCbdsn), so D stays unambiguous.
** The lengths other than those listed above have not been checked against the
** z/OS manual.
** A token matches a keyword when it is a prefix of it at least min long, so
** "PRI", "PRIM" and "PRIMARY" are one keyword while "PR" is none.  An exact
** name always wins, which is what keeps BLOCKS apart from BLOCKSIZE.
** Spellings FTPD took before this table (TRAILING, DATACLAS, STORCLAS,
** MGMTCLAS) are prefixes of the z/OS names and still match.
**
** A token that fits more than one keyword is refused like an unknown one:
** guessing which was meant would change a setting the client did not name.
**
** const on purpose: FTPD is AC(1), module data must not be written.
** ----------------------------------------------------------------- */
typedef struct site_keyword {
    const char  *name;
    int         min;
} site_keyword_t;

static const site_keyword_t site_keywords[] = {
    { "BLKSIZE",         3 },
    { "BLOCKSIZE",       7 },
    { "BLOCKS",          2 },
    { "CYLINDERS",       2 },
    { "DATACLASS",       5 },
    { "DCBDSN",          2 },
    { "DIRECTORY",       1 },
    { "FILETYPE",        4 },
    { "ISPFSTATS",       9 },
    { "JESJOBNAME",      7 },
    { "JESOWNER",        4 },
    { "JESSTATUS",       5 },
    { "LRECL",           2 },
    { "MGMTCLASS",       2 },
    { "PRIMARY",         3 },
    { "RDW",             3 },
    { "RECFM",           3 },
    { "RETPD",           3 },
    { "SBSENDEOL",       9 },
    { "SECONDARY",       3 },
    { "STORCLASS",       3 },
    { "TRACKS",          2 },
    { "TRAILINGBLANKS",  5 },
    { "TRUNCATE",        4 },
    { "UNIT",            1 },
    { "VOLUME",          3 },
    { NULL,              0 }
};

/* Full keyword name for an upper-case token, or NULL when the token names
** no keyword or more than one. */
static const char *
site_resolve(const char *key)
{
    const char *hit = NULL;
    int klen = (int)strlen(key);
    int nmatch = 0;

    if (klen == 0)
        return NULL;

    for (const site_keyword_t *kw = site_keywords; kw->name; kw++) {
        if (strcmp(key, kw->name) == 0)
            return kw->name;
        if (klen >= kw->min && strncmp(key, kw->name, klen) == 0) {
            hit = kw->name;
            nmatch++;
        }
    }

    return nmatch == 1 ? hit : NULL;
}

/* A whole decimal number 0..max.  atoi() maps "abc" and "" to 0, which for
** RETPD is a legitimate value, so the digits are checked here. */
static int
site_parse_range(const char *val, int max, int *out)
{
    long v = 0;

    if (val[0] == '\0' || strlen(val) > 9)
        return 0;
    for (int i = 0; val[i]; i++) {
        if (val[i] < '0' || val[i] > '9')
            return 0;
        v = v * 10 + (val[i] - '0');
    }
    if (v > max)
        return 0;
    *out = (int)v;
    return 1;
}

/* --------------------------------------------------------------------
** Copy a keyword value into a session field, upper-cased.
**
** Job names, user IDs and JES status names are upper case on MVS, and the
** other keyword values (RECFM, VOLUME, UNIT, FILETYPE) are folded already;
** a JES filter stored as typed would make "jesowner=ibmuser" and
** "JESOWNER=IBMUSER" two different settings (#189).
** ----------------------------------------------------------------- */
static void
site_copy_upper(char *dst, int dstsz, const char *val)
{
    int i;

    strncpy(dst, val, dstsz - 1);
    dst[dstsz - 1] = '\0';
    for (i = 0; dst[i]; i++)
        dst[i] = (char)toupper((unsigned char)dst[i]);
}

/* --------------------------------------------------------------------
** Record a warning for the final reply.
**
** First writer wins, matching how the unrecognized-parameter path keeps
** the FIRST offending token: one SITE command carries many parameters and
** the reply has room for one warning, so it should name the problem the
** client hit first rather than the last one that happened to overwrite it.
** ----------------------------------------------------------------- */
static void
site_warn(char *warn, int warnsz, const char *text)
{
    if (!warn || warnsz <= 0 || warn[0] != '\0')
        return;

    strncpy(warn, text, warnsz - 1);
    warn[warnsz - 1] = '\0';
}

/* --------------------------------------------------------------------
** Apply a single SITE KEY=VALUE parameter.
** Does not send any reply — caller sends one reply after all tokens.
** warn/warnsz: optional buffer filled with a warning message; caller
**   should include it in the final multi-line reply if non-empty.
** deny/denysz: filled (first one wins) when the server's policy refuses the
**   value; the caller answers 501 instead of 200 if it is non-empty.
** Returns 0 if recognized, 1 if unrecognized.
** ----------------------------------------------------------------- */
static int
site_apply_one(ftpd_session_t *sess,
               const char *key, const char *val,
               char *warn, int warnsz, char *deny, int denysz)
{
    const ftpd_config_t *cfg = &sess->server->config;

    /* --- Allocation parameters ---
    **
    ** A keyword given without a value cancels the setting on z/OS.  FTPD
    ** reads that as "back to the configured default", which is the same
    ** thing here: DEFRECFM/DEFLRECL/DEFBLKSIZE/DEFPRIMARY/DEFSECONDARY/
    ** DEFDIRECTORY are what the session started with, so returning to them
    ** is the only cancellation that has a meaning.  RETPD has no DEF key
    ** and cancels to no retention.
    **
    ** It also disposes of atoi("") == 0 without a special case: before
    ** #100 a bare SITE PRIMARY stored a primary quantity of zero and the
    ** allocation path quietly replaced it with 100 tracks. */

    if (strcmp(key, "RECFM") == 0) {
        char uval[16];
        int i;
        if (val[0] == '\0') {           /* bare keyword: back to default */
            strcpy(sess->alloc.recfm, cfg->defaults.recfm);
            return 0;
        }
        strncpy(uval, val, sizeof(uval) - 1);
        uval[sizeof(uval) - 1] = '\0';
        for (i = 0; uval[i]; i++)
            uval[i] = (char)toupper((unsigned char)uval[i]);
        strncpy(sess->alloc.recfm, uval, sizeof(sess->alloc.recfm) - 1);
        sess->alloc.recfm[sizeof(sess->alloc.recfm) - 1] = '\0';
        return 0;
    }

    if (strcmp(key, "LRECL") == 0) {
        if (val[0] == '\0')
            sess->alloc.lrecl = cfg->defaults.lrecl;
        else
            sess->alloc.lrecl = atoi(val);
        return 0;
    }

    if (strcmp(key, "BLKSIZE") == 0 || strcmp(key, "BLOCKSIZE") == 0) {
        int blk;
        if (val[0] == '\0') {
            sess->alloc.blksize = cfg->defaults.blksize;
            return 0;
        }
        blk = atoi(val);
        /* For FB, BLKSIZE must be a multiple of LRECL; adjust if needed */
        if (sess->alloc.recfm[0] == 'F' && sess->alloc.lrecl > 0 &&
            blk % sess->alloc.lrecl != 0) {
            blk = (blk / sess->alloc.lrecl) * sess->alloc.lrecl;
            if (blk == 0) blk = sess->alloc.lrecl;
            site_warn(warn, warnsz,
                "BLOCKSIZE must be a multiple of LRECL for RECFM FB");
        }
        sess->alloc.blksize = blk;
        return 0;
    }

    /* DCBDSN=model copies the model's RECFM, LRECL and BLKSIZE into the
    ** session right now.  Nothing remembers that a model was used, so a
    ** RECFM, LRECL or BLKSIZE given afterwards simply overwrites its part and
    ** a later DCBDSN overwrites all three: the last one given wins, with no
    ** state beyond the three values.  A bare DCBDSN returns all three to the
    ** configured defaults, like the keywords it stands in for.
    **
    ** A model that cannot be used is refused (501) and the current values stay:
    ** carrying on would let the next STOR create a data set with attributes
    ** the client believes it replaced. */
    if (strcmp(key, "DCBDSN") == 0) {
        ftpd_dcbmodel_t m;
        char dsn[FTPD_MAX_DSN_LEN + 3];
        char err[128];
        int len;
        int i;

        if (val[0] == '\0') {
            strcpy(sess->alloc.recfm, cfg->defaults.recfm);
            sess->alloc.lrecl = cfg->defaults.lrecl;
            sess->alloc.blksize = cfg->defaults.blksize;
            return 0;
        }

        /* The name is fully qualified, with or without quotes. */
        len = (int)strlen(val);
        i = (val[0] == '\'') ? 1 : 0;
        if (len > i && val[len - 1] == '\'')
            len--;
        len -= i;
        if (len < 1 || len > FTPD_MAX_DSN_LEN) {
            if (deny[0] == '\0')
                snprintf(deny, denysz, "DCBDSN: not a data set name; "
                         "keeping the current values");
            return 0;
        }
        site_copy_upper(dsn, sizeof(dsn), val + i);
        dsn[len] = '\0';

        if (strchr(dsn, '(') != NULL || !ftpd_dsn_valid(dsn, 0)) {
            if (deny[0] == '\0')
                snprintf(deny, denysz, "DCBDSN: %s is not a plain data set "
                         "name; keeping the current values", dsn);
            return 0;
        }

        if (ftpd_mvs_dcbmodel(sess, dsn, &m, err, sizeof(err)) != 0) {
            if (deny[0] == '\0')
                snprintf(deny, denysz, "%s; keeping the current values", err);
            return 0;
        }

        strcpy(sess->alloc.recfm, m.recfm);
        sess->alloc.lrecl = m.lrecl;
        sess->alloc.blksize = m.blksize;
        if (m.note[0] != '\0')
            site_warn(warn, warnsz, m.note);
        return 0;
    }

    if (strcmp(key, "PRIMARY") == 0) {
        /* 1..16777215 on z/OS.  Out of range leaves the current setting
        ** alone and says so: the old code stored whatever atoi() produced
        ** -- 0 for "PRIMARY=" and for "PRIMARY=abc" alike -- and the
        ** allocation path then substituted a number nobody had asked for
        ** (#100). */
        int v;
        if (val[0] == '\0') {
            sess->alloc.primary = cfg->defaults.primary;
            return 0;
        }
        v = atoi(val);
        if (v < 1 || v > FTPD_SPACE_MAX)
            site_warn(warn, warnsz,
                "PRIMARY must be 1-16777215; keeping the current value");
        else
            sess->alloc.primary = v;
        return 0;
    }

    if (strcmp(key, "SECONDARY") == 0) {
        /* 0..16777215.  Unlike PRIMARY, 0 is a real request -- "no
        ** secondary extents" -- and must reach the allocation. */
        int v;
        if (val[0] == '\0') {
            sess->alloc.secondary = cfg->defaults.secondary;
            return 0;
        }
        v = atoi(val);
        if (v < 0 || v > FTPD_SPACE_MAX)
            site_warn(warn, warnsz,
                "SECONDARY must be 0-16777215; keeping the current value");
        else
            sess->alloc.secondary = v;
        return 0;
    }

    if (strcmp(key, "DIRECTORY") == 0) {
        /* Bare DIRECTORY goes back to DEFDIRECTORY like every other
        ** allocation keyword. */
        int v;
        if (val[0] == '\0') {
            sess->alloc.dirblks = cfg->defaults.directory;
            return 0;
        }
        if (!site_parse_range(val, FTPD_SPACE_MAX, &v) || v < 1)
            site_warn(warn, warnsz,
                "DIRECTORY must be 1-16777215; keeping the current value");
        else
            sess->alloc.dirblks = v;
        return 0;
    }

    if (strcmp(key, "RETPD") == 0) {
        /* Days, 0-9999 as on z/OS.  0 is a real value (no retention), so
        ** only a bare RETPD resets it. */
        int v;
        if (val[0] == '\0') {
            sess->alloc.retpd = 0;
            return 0;
        }
        if (!site_parse_range(val, FTPD_RETPD_MAX, &v))
            site_warn(warn, warnsz,
                "RETPD must be 0-9999; keeping the current value");
        else if (v > cfg->max_retpd) {
            /* The server's ceiling (MAXRETPD, #201).  A policy refusal, not
            ** a typo: it gets a 5xx of its own instead of the 200 warning. */
            if (deny[0] == '\0')
                snprintf(deny, denysz,
                    "RETPD=%d exceeds the server limit of %d days; "
                    "keeping the current value", v, cfg->max_retpd);
        } else
            sess->alloc.retpd = v;
        return 0;
    }

    if (strcmp(key, "TRACKS") == 0) {
        strcpy(sess->alloc.spacetype, "TRK");
        return 0;
    }

    if (strcmp(key, "CYLINDERS") == 0) {
        strcpy(sess->alloc.spacetype, "CYL");
        return 0;
    }

    if (strcmp(key, "BLOCKS") == 0) {
        /* BLOCKS counts the space in units of the current BLKSIZE, so
        ** without one there is nothing to count -- refuse rather than
        ** allocate against a zero-length block. */
        if (sess->alloc.blksize <= 0) {
            site_warn(warn, warnsz,
                "BLOCKS needs a non-zero BLKSIZE; allocation unit unchanged");
            return 0;
        }
        strcpy(sess->alloc.spacetype, "BLK");
        return 0;
    }

    if (strcmp(key, "VOLUME") == 0) {
        char uval[8];
        int i;
        /* Bare VOLUME goes back to the configured default, like every other
        ** allocation keyword. */
        if (val[0] == '\0') {
            strcpy(sess->alloc.volume, cfg->defaults.volume);
            return 0;
        }
        strncpy(uval, val, sizeof(uval) - 1);
        uval[sizeof(uval) - 1] = '\0';
        for (i = 0; uval[i]; i++)
            uval[i] = (char)toupper((unsigned char)uval[i]);
        /* A volume that is not mounted must be refused HERE.  Letting it
        ** reach SVC 99 does not fail the allocation -- it asks the operator
        ** (IEF238D REPLY DEVICE NAME OR 'CANCEL') and waits, so the client's
        ** session hangs with a WTOR sitting on the console until somebody
        ** answers it.  Measured on mvsdev 2026-09-13 with SITE VOLUME=NOVOL9:
        ** no reply after the 229, and two outstanding requests on the console.
        **
        ** Refusing the value and keeping the current one is the same shape as
        ** an out-of-range PRIMARY, and it answers the client in the session
        ** where it asked.  RESIDUAL: a volume that goes offline between this
        ** check and the transfer is back to the operator prompt; closing that
        ** needs S99NOMNT on the allocation itself, which __dsalc() does not
        ** set (mvslovers/libc370#181). */
        if (!ftpdcfg_volume_online(uval, NULL)) {
            site_warn(warn, warnsz,
                "VOLUME is not mounted; keeping the current value");
            return 0;
        }

        strncpy(sess->alloc.volume, uval, sizeof(sess->alloc.volume) - 1);
        sess->alloc.volume[sizeof(sess->alloc.volume) - 1] = '\0';
        return 0;
    }

    if (strcmp(key, "UNIT") == 0) {
        char uval[8];
        int i;
        if (val[0] == '\0') {
            strcpy(sess->alloc.unit, cfg->defaults.unit);
            return 0;
        }
        strncpy(uval, val, sizeof(uval) - 1);
        uval[sizeof(uval) - 1] = '\0';
        for (i = 0; uval[i]; i++)
            uval[i] = (char)toupper((unsigned char)uval[i]);
        strncpy(sess->alloc.unit, uval, sizeof(sess->alloc.unit) - 1);
        sess->alloc.unit[sizeof(sess->alloc.unit) - 1] = '\0';
        return 0;
    }

    /* --- Mode switches --- */

    if (strcmp(key, "FILETYPE") == 0) {
        char uval[8];
        int i;
        strncpy(uval, val, sizeof(uval) - 1);
        uval[sizeof(uval) - 1] = '\0';
        for (i = 0; uval[i]; i++)
            uval[i] = (char)toupper((unsigned char)uval[i]);
        if (strcmp(uval, "JES") == 0) {
            sess->prev_fsmode = sess->fsmode;
            sess->filetype = FT_JES;
        } else {
            sess->filetype = FT_SEQ;
            sess->fsmode = sess->prev_fsmode;
        }
        return 0;
    }

    /* --- Transfer modifiers (toggles) --- */

    if (strcmp(key, "TRAILINGBLANKS") == 0) {
        sess->trailing = !sess->trailing;
        return 0;
    }

    /* Toggles the TRUNCATE flag that STOR/APPE read: with it a text line
    ** longer than the record is cut and the 250 says so, without it the
    ** transfer fails (#207). */
    if (strcmp(key, "TRUNCATE") == 0) {
        sess->truncate = !sess->truncate;
        return 0;
    }

    /* RDW is refused: a client asking for it wants binary V records
    ** with their record descriptor words kept on STOR and RETR, and FTPD's
    ** transfer paths know no such mode.  Accepting the toggle and doing
    ** nothing would hand the client a data set with the RDWs missing or
    ** doubled (#207). */
    if (strcmp(key, "RDW") == 0) {
        if (deny[0] == '\0')
            snprintf(deny, denysz,
                "SITE RDW is not supported: FTPD does not keep record "
                "descriptor words");
        return 0;
    }

    /* The only line end FTPD writes and reads is CRLF, and CRLF is also the
    ** z/OS default.  The other values (SBSENDEOL=LF, CR, NL, LFCR) change
    ** how a text file is cut into lines, which FTPD does not do. */
    if (strcmp(key, "SBSENDEOL") == 0) {
        int i;
        char uval[8];

        strncpy(uval, val, sizeof(uval) - 1);
        uval[sizeof(uval) - 1] = '\0';
        for (i = 0; uval[i]; i++)
            uval[i] = (char)toupper((unsigned char)uval[i]);
        if (uval[0] != '\0' && strcmp(uval, "CRLF") != 0 && deny[0] == '\0')
            snprintf(deny, denysz,
                "SBSENDEOL=%s is not supported: FTPD only handles CRLF", val);
        return 0;
    }

    /* --- JES parameters --- */

    /* JESINTERFACELEVEL is deliberately NOT accepted here.  On z/OS it is a
    ** server setting in FTP.DATA, not something a client may change per
    ** session, and z/OS answers a client that tries with "Unrecognized
    ** parameter".  FTPD does the same by falling through to the
    ** unrecognized-parameter path below.  What the level governs on z/OS --
    ** who may see and fetch another user's spool -- is tracked as its own
    ** piece of work. */

    if (strcmp(key, "JESJOBNAME") == 0) {
        site_copy_upper(sess->jes_jobname, sizeof(sess->jes_jobname), val);
        return 0;
    }

    /* A listing filter that narrows to somebody else's jobs needs the right
    ** to see them (#90).  The value is checked whole, before anything cuts
    ** it to the size of the field.  A well formed value without the right is
    ** ignored with a 200 that names the filter that stays, as z/OS does
    ** ("User x is not authorized to filter on JESOWNER, JESOWNER remains
    ** x"): Zowe sends JESOWNER=* with every job request and stops on a 5xx
    ** (#398).  A value that is no owner at all stays a 501. */
    if (strcmp(key, "JESOWNER") == 0) {
        char canon[FTPD_JWN_PRTSZ];
        int ok = ftpd_jes_owner_ok(sess, val, canon, sizeof(canon));

        if (ok == FTPD_JES_OWNER_OK) {
            strcpy(sess->jes_owner, canon);
        } else if (ok == FTPD_JES_OWNER_DENIED) {
            char who[FTPD_JWN_PRTSZ];
            char cur[FTPD_JWN_PRTSZ];
            char msg[96];

            ftpd_jwn_printable(who, sizeof(who), sess->user);
            ftpd_jwn_printable(cur, sizeof(cur),
                               sess->jes_owner[0] ? sess->jes_owner
                                                  : sess->user);
            snprintf(msg, sizeof(msg),
                "User %s is not authorized to filter on JESOWNER, "
                "JESOWNER remains %s", who, cur);
            site_warn(warn, warnsz, msg);
        } else if (deny[0] == '\0') {
            snprintf(deny, denysz, "JESOWNER is not permitted");
        }
        return 0;
    }

    /* ALL, INPUT, ACTIVE or OUTPUT.  LIST, RETR and DELE in JES mode show
    ** only the jobs in that state; an empty value goes back to ALL (#207). */
    if (strcmp(key, "JESSTATUS") == 0) {
        const char *name = ftpd_jfl_name(val);

        if (name == NULL)
            site_warn(warn, warnsz,
                "JESSTATUS must be ALL, INPUT, ACTIVE or OUTPUT; "
                "keeping the current value");
        else
            site_copy_upper(sess->jes_status, sizeof(sess->jes_status),
                            name);
        return 0;
    }

    /* On z/OS it makes a STOR write ISPF statistics into the member's
    ** directory entry.  FTPD never writes them, so there is nothing to
    ** switch, but the Zowe FTP plugin sends it with every transfer and
    ** reads the "Unrecognized parameter" reply as a failure (#302). */
    if (strcmp(key, "ISPFSTATS") == 0)
        return 0;

    /* --- SMS classes (accepted silently, no SMS on MVS 3.8j) --- */

    if (strcmp(key, "DATACLASS") == 0 ||
        strcmp(key, "STORCLASS") == 0 ||
        strcmp(key, "MGMTCLASS") == 0) {
        return 0;
    }

    return 1; /* unrecognized */
}

/* --------------------------------------------------------------------
** SITE command dispatcher
**
** Tokenizes the argument on whitespace and applies each KEY=VALUE
** token via site_apply_one().  Sends exactly one reply after all
** tokens are processed, regardless of how many parameters were given.
** ----------------------------------------------------------------- */
int
ftpd_site_dispatch(ftpd_session_t *sess, const char *arg)
{
    char tok[96];
    char key[32];
    char val[64];
    char warn[128];
    char deny[128];             /* first value the server's policy refused */
    char badtok[96];            /* first unrecognized token, verbatim */
    char badmsg[160];
    const char *p;
    const char *end;
    const char *full;
    int tlen;
    int n_tokens;
    int n_unrecognized;

    /* Bare SITE (no arguments) */
    if (!arg || arg[0] == '\0') {
        ftpd_session_reply(sess, FTP_202,
            "SITE not necessary; you may proceed");
        return 0;
    }

#ifdef FTPD_DEBUG_ABEND
    /* Debug-only ABEND injection for per-command recovery testing (#63).
    ** NOT compiled into production builds — enable with -DFTPD_DEBUG_ABEND.
    ** Requires an authenticated session (SITE is already post-auth; the
    ** explicit check keeps this from ever being a pre-auth DoS).  Keyword
    ** must be upper-case:
    **   SITE ABEND        -> ABEND (S0C4) OUTSIDE any identity window;
    **                        recovery's unlock() must be a no-op and leave
    **                        every concurrent window undisturbed.
    **   SITE ABEND=LOCK   -> open an identity window (ACEE switched, ENQ
    **                        held), then ABEND inside it; recovery must put
    **                        the STC identity back AND release the ENQ, or
    **                        every other session's data set access stalls.
    **   SITE ABEND=XFER   -> arm the next MVS RETR to ABEND mid-transfer
    **                        (cur_file set + data connection open); exercises
    **                        recovery's fclose(cur_file) + clear-before-close.
    ** All must yield: client gets 451, worker stays up, next command works.
    ** NB: S0C4 is a clean program check at a defined instruction; the messier
    ** realistic cases (S013/S001/S878) are not reproduced here — this proves
    ** the recovery MECHANISM, not the hardest DCB/buffer states. */
    if (sess->authenticated && strncmp(arg, "ABEND", 5) == 0) {
        volatile int *trap = (volatile int *)0;

        /* ABEND=XFER: arm, do not ABEND now. */
        if (arg[5] == '=' && (arg[6] == 'X' || arg[6] == 'x')) {
            sess->debug_abend_xfer = 1;
            ftpd_log_wto("FTPD072W DEBUG ABEND=XFER ARMED SOCKET=%d",
                         sess->ctrl_sock);
            ftpd_session_reply(sess, FTP_200,
                "DEBUG: next RETR will ABEND mid-transfer");
            return 0;
        }

        /* ABEND=LOCK: ABEND with the identity window open. */
        if (arg[5] == '=' && (arg[6] == 'L' || arg[6] == 'l'))
            ftpd_acee_enter(sess);

        ftpd_log_wto("FTPD072W DEBUG ABEND INJECTION: SITE %s", arg);
        *trap = 0;                     /* force S0C4 */
    }
#endif

    warn[0] = '\0';
    deny[0] = '\0';
    badtok[0] = '\0';
    n_tokens = 0;
    n_unrecognized = 0;
    p = arg;

    while (*p) {
        /* Skip whitespace */
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

        /* Find end of token */
        end = p;
        while (*end && *end != ' ' && *end != '\t') end++;

        /* Copy token */
        tlen = end - p;
        if (tlen >= (int)sizeof(tok)) tlen = (int)sizeof(tok) - 1;
        memcpy(tok, p, tlen);
        tok[tlen] = '\0';

        parse_kv(tok, key, sizeof(key), val, sizeof(val));
        full = site_resolve(key);
        if (!full || site_apply_one(sess, full, val, warn, sizeof(warn),
                                  deny, sizeof(deny)) != 0) {
            /* Keep the first offender verbatim for the reply — z/OS names it,
            ** and "which one?" is the only question the old wording left. */
            if (badtok[0] == '\0') {
                strncpy(badtok, tok, sizeof(badtok) - 1);
                badtok[sizeof(badtok) - 1] = '\0';
            }
            n_unrecognized++;
        }
        n_tokens++;
        p = end;
    }

    /* No tokens after stripping whitespace — treat as bare SITE */
    if (n_tokens == 0) {
        ftpd_session_reply(sess, FTP_202,
            "SITE not necessary; you may proceed");
        return 0;
    }

    /* Send exactly one reply for the entire command */
    if (deny[0] != '\0') {
        /* The other parameters of the line were applied: SITE has no
        ** all-or-nothing, so a 5xx alone would read as "nothing changed". */
        if (n_tokens > 1)
            ftpd_session_reply_multi(sess, FTP_501, deny,
                "Other parameters on this command were applied.");
        else
            ftpd_session_reply(sess, FTP_501, "%s", deny);
    } else if (n_unrecognized > 0) {
        snprintf(badmsg, sizeof(badmsg),
                 "Unrecognized parameter '%s' on SITE command.", badtok);
        ftpd_session_reply_multi(sess, FTP_200,
            badmsg,
            "SITE command was accepted");
    } else if (warn[0] != '\0') {
        ftpd_session_reply_multi(sess, FTP_200,
            warn,
            "SITE command was accepted");
    } else {
        ftpd_session_reply(sess, FTP_200, "SITE command was accepted");
    }

    return 0;
}
