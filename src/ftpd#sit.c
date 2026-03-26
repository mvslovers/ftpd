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

/* --------------------------------------------------------------------
** Helper: parse "KEY=VALUE" from arg.
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
** SITE command dispatcher
** ----------------------------------------------------------------- */
int
ftpd_site_dispatch(ftpd_session_t *sess, const char *arg)
{
    char key[32];
    char val[64];

    /* Bare SITE (no arguments) */
    if (!arg || !arg[0] || arg[0] == '\0') {
        ftpd_session_reply(sess, FTP_202,
            "SITE not necessary; you may proceed");
        return 0;
    }

    parse_kv(arg, key, sizeof(key), val, sizeof(val));

    /* --- Allocation parameters --- */

    if (strcmp(key, "RECFM") == 0) {
        /* Uppercase value */
        int i;
        for (i = 0; val[i]; i++)
            val[i] = (char)toupper((unsigned char)val[i]);
        strncpy(sess->alloc.recfm, val, sizeof(sess->alloc.recfm) - 1);
        sess->alloc.recfm[sizeof(sess->alloc.recfm) - 1] = '\0';
        ftpd_session_reply(sess, FTP_200, "SITE command was accepted");
        return 0;
    }

    if (strcmp(key, "LRECL") == 0) {
        sess->alloc.lrecl = atoi(val);
        ftpd_session_reply(sess, FTP_200, "SITE command was accepted");
        return 0;
    }

    if (strcmp(key, "BLKSIZE") == 0) {
        int blk = atoi(val);
        /* Validate: for FB, BLKSIZE must be multiple of LRECL */
        if (sess->alloc.recfm[0] == 'F' && sess->alloc.lrecl > 0 &&
            blk % sess->alloc.lrecl != 0) {
            blk = (blk / sess->alloc.lrecl) * sess->alloc.lrecl;
            if (blk == 0) blk = sess->alloc.lrecl;
            ftpd_session_reply_multi(sess, FTP_200,
                "BLOCKSIZE must be a multiple of LRECL for RECFM FB",
                "SITE command was accepted");
            sess->alloc.blksize = blk;
            return 0;
        }
        sess->alloc.blksize = blk;
        ftpd_session_reply(sess, FTP_200, "SITE command was accepted");
        return 0;
    }

    if (strcmp(key, "PRIMARY") == 0) {
        sess->alloc.primary = atoi(val);
        ftpd_session_reply(sess, FTP_200, "SITE command was accepted");
        return 0;
    }

    if (strcmp(key, "SECONDARY") == 0) {
        sess->alloc.secondary = atoi(val);
        ftpd_session_reply(sess, FTP_200, "SITE command was accepted");
        return 0;
    }

    if (strcmp(key, "DIRECTORY") == 0) {
        sess->alloc.dirblks = atoi(val);
        ftpd_session_reply(sess, FTP_200, "SITE command was accepted");
        return 0;
    }

    if (strcmp(key, "TRACKS") == 0) {
        strcpy(sess->alloc.spacetype, "TRK");
        ftpd_session_reply(sess, FTP_200, "SITE command was accepted");
        return 0;
    }

    if (strcmp(key, "CYLINDERS") == 0) {
        strcpy(sess->alloc.spacetype, "CYL");
        ftpd_session_reply(sess, FTP_200, "SITE command was accepted");
        return 0;
    }

    if (strcmp(key, "VOLUME") == 0) {
        int i;
        for (i = 0; val[i]; i++)
            val[i] = (char)toupper((unsigned char)val[i]);
        strncpy(sess->alloc.volume, val, sizeof(sess->alloc.volume) - 1);
        sess->alloc.volume[sizeof(sess->alloc.volume) - 1] = '\0';
        ftpd_session_reply(sess, FTP_200, "SITE command was accepted");
        return 0;
    }

    if (strcmp(key, "UNIT") == 0) {
        int i;
        for (i = 0; val[i]; i++)
            val[i] = (char)toupper((unsigned char)val[i]);
        strncpy(sess->alloc.unit, val, sizeof(sess->alloc.unit) - 1);
        sess->alloc.unit[sizeof(sess->alloc.unit) - 1] = '\0';
        ftpd_session_reply(sess, FTP_200, "SITE command was accepted");
        return 0;
    }

    /* --- Mode switches --- */

    if (strcmp(key, "FILETYPE") == 0) {
        int i;
        for (i = 0; val[i]; i++)
            val[i] = (char)toupper((unsigned char)val[i]);
        if (strcmp(val, "JES") == 0) {
            sess->filetype = FT_JES;
        } else {
            sess->filetype = FT_SEQ;
        }
        ftpd_session_reply(sess, FTP_200, "SITE command was accepted");
        return 0;
    }

    /* --- Transfer modifiers (toggles) --- */

    if (strcmp(key, "TRAILING") == 0) {
        sess->trailing = !sess->trailing;
        ftpd_session_reply(sess, FTP_200, "SITE command was accepted");
        return 0;
    }

    if (strcmp(key, "TRUNCATE") == 0) {
        sess->truncate = !sess->truncate;
        ftpd_session_reply(sess, FTP_200, "SITE command was accepted");
        return 0;
    }

    if (strcmp(key, "RDW") == 0) {
        sess->rdw = !sess->rdw;
        ftpd_session_reply(sess, FTP_200, "SITE command was accepted");
        return 0;
    }

    if (strcmp(key, "SBSENDEOL") == 0) {
        /* Accept but no special handling needed yet */
        ftpd_session_reply(sess, FTP_200, "SITE command was accepted");
        return 0;
    }

    /* --- JES parameters (stored for Phase 2) --- */

    if (strcmp(key, "JESINTERFACELEVEL") == 0) {
        sess->jes_level = atoi(val);
        ftpd_session_reply(sess, FTP_200, "SITE command was accepted");
        return 0;
    }

    if (strcmp(key, "JESJOBNAME") == 0) {
        strncpy(sess->jes_jobname, val, sizeof(sess->jes_jobname) - 1);
        sess->jes_jobname[sizeof(sess->jes_jobname) - 1] = '\0';
        ftpd_session_reply(sess, FTP_200, "SITE command was accepted");
        return 0;
    }

    if (strcmp(key, "JESOWNER") == 0) {
        strncpy(sess->jes_owner, val, sizeof(sess->jes_owner) - 1);
        sess->jes_owner[sizeof(sess->jes_owner) - 1] = '\0';
        ftpd_session_reply(sess, FTP_200, "SITE command was accepted");
        return 0;
    }

    if (strcmp(key, "JESSTATUS") == 0) {
        strncpy(sess->jes_status, val, sizeof(sess->jes_status) - 1);
        sess->jes_status[sizeof(sess->jes_status) - 1] = '\0';
        ftpd_session_reply(sess, FTP_200, "SITE command was accepted");
        return 0;
    }

    /* --- SMS classes (accepted silently, no SMS on MVS 3.8j) --- */

    if (strcmp(key, "DATACLAS") == 0 ||
        strcmp(key, "STORCLAS") == 0 ||
        strcmp(key, "MGMTCLAS") == 0) {
        ftpd_session_reply(sess, FTP_200, "SITE command was accepted");
        return 0;
    }

    /* --- Unknown parameter --- */
    ftpd_session_reply_multi(sess, FTP_200,
        "Unrecognized parameter on SITE command.",
        "SITE command was accepted");

    return 0;
}
