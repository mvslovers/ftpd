/*
** FTPD SITE Command Processing
**
** z/OS-compatible SITE subcommands for allocation parameters,
** transfer modifiers, and JES interface settings.
**
** All recognized parameters return "200 SITE command was accepted".
** Unknown parameters return "200-Unrecognized parameter ... / 200 SITE ...".
** Bare SITE (no args) returns "202 SITE not necessary; you may proceed".
*/
#include "ftpd.h"
#include "ftpd#ses.h"
#include "ftpd#sit.h"
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
** Apply a single SITE KEY=VALUE parameter.
** Does not send any reply — caller sends one reply after all tokens.
** warn/warnsz: optional buffer filled with a warning message; caller
**   should include it in the final multi-line reply if non-empty.
** Returns 0 if recognized, 1 if unrecognized.
** ----------------------------------------------------------------- */
static int
site_apply_one(ftpd_session_t *sess,
               const char *key, const char *val,
               char *warn, int warnsz)
{
    /* --- Allocation parameters --- */

    if (strcmp(key, "RECFM") == 0) {
        char uval[16];
        int i;
        strncpy(uval, val, sizeof(uval) - 1);
        uval[sizeof(uval) - 1] = '\0';
        for (i = 0; uval[i]; i++)
            uval[i] = (char)toupper((unsigned char)uval[i]);
        strncpy(sess->alloc.recfm, uval, sizeof(sess->alloc.recfm) - 1);
        sess->alloc.recfm[sizeof(sess->alloc.recfm) - 1] = '\0';
        return 0;
    }

    if (strcmp(key, "LRECL") == 0) {
        sess->alloc.lrecl = atoi(val);
        return 0;
    }

    if (strcmp(key, "BLKSIZE") == 0) {
        int blk = atoi(val);
        /* For FB, BLKSIZE must be a multiple of LRECL; adjust if needed */
        if (sess->alloc.recfm[0] == 'F' && sess->alloc.lrecl > 0 &&
            blk % sess->alloc.lrecl != 0) {
            blk = (blk / sess->alloc.lrecl) * sess->alloc.lrecl;
            if (blk == 0) blk = sess->alloc.lrecl;
            if (warn && warnsz > 0)
                strncpy(warn,
                    "BLOCKSIZE must be a multiple of LRECL for RECFM FB",
                    warnsz - 1);
        }
        sess->alloc.blksize = blk;
        return 0;
    }

    if (strcmp(key, "PRIMARY") == 0) {
        sess->alloc.primary = atoi(val);
        return 0;
    }

    if (strcmp(key, "SECONDARY") == 0) {
        sess->alloc.secondary = atoi(val);
        return 0;
    }

    if (strcmp(key, "DIRECTORY") == 0) {
        sess->alloc.dirblks = atoi(val);
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

    if (strcmp(key, "VOLUME") == 0) {
        char uval[8];
        int i;
        strncpy(uval, val, sizeof(uval) - 1);
        uval[sizeof(uval) - 1] = '\0';
        for (i = 0; uval[i]; i++)
            uval[i] = (char)toupper((unsigned char)uval[i]);
        strncpy(sess->alloc.volume, uval, sizeof(sess->alloc.volume) - 1);
        sess->alloc.volume[sizeof(sess->alloc.volume) - 1] = '\0';
        return 0;
    }

    if (strcmp(key, "UNIT") == 0) {
        char uval[8];
        int i;
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

    if (strcmp(key, "TRAILING") == 0) {
        sess->trailing = !sess->trailing;
        return 0;
    }

    if (strcmp(key, "TRUNCATE") == 0) {
        sess->truncate = !sess->truncate;
        return 0;
    }

    if (strcmp(key, "RDW") == 0) {
        sess->rdw = !sess->rdw;
        return 0;
    }

    if (strcmp(key, "SBSENDEOL") == 0) {
        return 0;
    }

    /* --- JES parameters --- */

    /* JESINTERFACELEVEL is deliberately NOT accepted here.  On z/OS it is a
    ** server setting in FTP.DATA, not something a client may change per
    ** session, and z/OS answers a client that tries with "Unrecognized
    ** parameter".  Accepting it was worse than either: ftpd stored the value
    ** in sess->jes_level and no code has ever read it, so the 200 reply
    ** promised a scoping level that does not exist.  What that level governs
    ** on z/OS -- who may see and fetch another user's spool -- is tracked as
    ** its own piece of work; until it exists, say so honestly by falling
    ** through to the unrecognized-parameter path below. */

    if (strcmp(key, "JESJOBNAME") == 0) {
        strncpy(sess->jes_jobname, val, sizeof(sess->jes_jobname) - 1);
        sess->jes_jobname[sizeof(sess->jes_jobname) - 1] = '\0';
        return 0;
    }

    if (strcmp(key, "JESOWNER") == 0) {
        strncpy(sess->jes_owner, val, sizeof(sess->jes_owner) - 1);
        sess->jes_owner[sizeof(sess->jes_owner) - 1] = '\0';
        return 0;
    }

    if (strcmp(key, "JESSTATUS") == 0) {
        strncpy(sess->jes_status, val, sizeof(sess->jes_status) - 1);
        sess->jes_status[sizeof(sess->jes_status) - 1] = '\0';
        return 0;
    }

    /* --- SMS classes (accepted silently, no SMS on MVS 3.8j) --- */

    if (strcmp(key, "DATACLAS") == 0 ||
        strcmp(key, "STORCLAS") == 0 ||
        strcmp(key, "MGMTCLAS") == 0) {
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
    char badtok[96];            /* first unrecognized token, verbatim */
    char badmsg[160];
    const char *p;
    const char *end;
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
        if (site_apply_one(sess, key, val, warn, sizeof(warn)) != 0) {
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
    if (n_unrecognized > 0) {
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
