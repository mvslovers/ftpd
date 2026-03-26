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
resolve_dsn(ftpd_session_t *sess, const char *arg, char *buf, int bufsz)
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

    /* Reject wildcards in CWD/RETR/STOR context */
    if (strchr(arg, '*') || strchr(arg, '%')) {
        return -1;
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
int
ftpd_mvs_cwd(ftpd_session_t *sess, const char *arg)
{
    char dsn[FTPD_MAX_DSN_LEN + 2];

    if (resolve_dsn(sess, arg, dsn, sizeof(dsn)) != 0) {
        ftpd_session_reply(sess, FTP_501,
                           "Invalid dataset name");
        return 0;
    }

    /* Ensure trailing dot for prefix matching */
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

    ftpd_session_reply(sess, FTP_250,
                       "\"'%s'\" is the working directory name prefix.",
                       sess->mvs_cwd);

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
                ftpd_data_printf(sess,
                    " %-8s %5s %8s %17s %5s %5s %5s %-8s\r\n",
                    ist.name, ist.ver, ist.created, ist.changed,
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
** ----------------------------------------------------------------- */
int
ftpd_mvs_list(ftpd_session_t *sess, const char *arg, int nlst)
{
    char prefix[FTPD_MAX_DSN_LEN + 2];
    int is_pds;

    /* Build the listing prefix */
    if (arg && arg[0]) {
        if (resolve_dsn(sess, arg, prefix, sizeof(prefix)) != 0) {
            ftpd_session_reply(sess, FTP_501, "Invalid dataset name");
            return 0;
        }
    } else {
        /* No arg: list current CWD prefix */
        strncpy(prefix, sess->mvs_cwd, sizeof(prefix) - 1);
        prefix[sizeof(prefix) - 1] = '\0';
        /* Strip trailing dot for is_pds check */
        {
            int len = strlen(prefix);
            if (len > 0 && prefix[len - 1] == '.')
                prefix[len - 1] = '\0';
        }
    }

    /* Check if this is a PDS — if so, list members */
    is_pds = ftpd_mvs_is_pds(prefix);

    /* Open data connection */
    ftpd_session_reply(sess, FTP_150,
                       "Opening data connection for file list");
    if (ftpd_data_open(sess) != 0) {
        ftpd_session_reply(sess, FTP_425,
                           "Cannot open data connection");
        return 0;
    }

    if (is_pds == 1) {
        /* PDS member listing */
        PDSLIST **pds;
        DSLIST **dsl;
        char recfm[5];
        int i;

        /* Get RECFM for formatting */
        recfm[0] = '\0';
        dsl = __listds(prefix, "NONVSAM VOLUME", NULL);
        if (dsl && dsl[0])
            strncpy(recfm, dsl[0]->recfm, sizeof(recfm) - 1);
        if (dsl)
            __freeds(&dsl);

        /* List header for PDS */
        if (!nlst) {
            if (recfm[0] != 'U') {
                ftpd_data_printf(sess,
                    " Name     VV.MM   Created   Changed"
                    "            Size  Init   Mod   Id\r\n");
            } else {
                ftpd_data_printf(sess,
                    " Name       Size   TTR    AC  Attributes\r\n");
            }
        }

        pds = __listpd(prefix, NULL);
        if (pds) {
            for (i = 0; pds[i]; i++)
                send_pds_entry(sess, pds[i], nlst, recfm);
            __freepd(&pds);
        }
    } else {
        /* Dataset listing */
        DSLIST **dsl;
        char level[FTPD_MAX_DSN_LEN + 2];
        int i;

        /* Build the catalog search level (strip trailing dot) */
        strncpy(level, prefix, sizeof(level) - 1);
        level[sizeof(level) - 1] = '\0';
        {
            int len = strlen(level);
            if (len > 0 && level[len - 1] == '.')
                level[len - 1] = '\0';
        }

        /* List header (z/OS format) */
        if (!nlst) {
            ftpd_data_printf(sess,
                "Volume Unit    Referred Ext Used Recfm "
                "Lrecl BlkSz Dsorg Dsname\r\n");
        }

        dsl = __listds(level, "NONVSAM VOLUME", NULL);
        if (dsl) {
            for (i = 0; dsl[i]; i++)
                send_ds_entry(sess, dsl[i], nlst, sess->mvs_cwd);
            __freeds(&dsl);
        }
    }

    ftpd_data_close(sess);
    ftpd_session_reply(sess, FTP_226, "Transfer complete");

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

    if (resolve_dsn(sess, dsn, name, sizeof(name)) != 0)
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
