/*
** FTPD Command Parser & Dispatcher
**
** FTP protocol command handling (client-facing).
** Response strings match z/OS FTP Server behavior
** (see doc/ZOS_FTP_REFERENCE.md).
*/
#include "ftpd.h"
#include "ftpd#ses.h"
#include "ftpd#cmd.h"
#include "ftpd#dat.h"
#include "ftpd#aut.h"
#include "ftpd#mvs.h"
#include "ftpd#ufs.h"
#include "ftpd#jes.h"
#include "ftpd#sit.h"

/* --------------------------------------------------------------------
** Helper: return human-readable name for the current TYPE setting.
** Matches z/OS format: "Ascii NonPrint", "Image", "Ebcdic NonPrint".
** ----------------------------------------------------------------- */
static const char *
type_name(char t)
{
    switch (t) {
    case XFER_TYPE_A: return "Ascii NonPrint";
    case XFER_TYPE_I: return "Image";
    case XFER_TYPE_E: return "Ebcdic NonPrint";
    default:          return "Ascii NonPrint";
    }
}

/* --------------------------------------------------------------------
** Helper: return human-readable name for the current STRU setting.
** Matches z/OS format: "File", "Record".
** ----------------------------------------------------------------- */
static const char *
stru_name(char s)
{
    switch (s) {
    case XFER_STRU_F: return "File";
    case XFER_STRU_R: return "Record";
    default:          return "File";
    }
}

/* --------------------------------------------------------------------
** cmd_syst -- SYST: mode-aware system identification
** ----------------------------------------------------------------- */
static int
cmd_syst(ftpd_session_t *sess)
{
    if (sess->fsmode == FS_UFS) {
        ftpd_session_reply(sess, FTP_215,
            "UNIX is the operating system of this server. "
            "FTP Server is running on MVS.");
    } else {
        ftpd_session_reply(sess, FTP_215,
            "MVS is the operating system of this server. "
            "FTP Server is running on MVS.");
    }
    return 0;
}

/* --------------------------------------------------------------------
** cmd_feat -- FEAT: feature list (RFC 2389)
** ----------------------------------------------------------------- */
static int
cmd_feat(ftpd_session_t *sess)
{
    char buf[256];
    int len, i;
    len = snprintf(buf, sizeof(buf),
        "211-Features supported\r\n"
        " EPSV\r\n"
        " SIZE\r\n"
        " MDTM\r\n"
        " SITE FILETYPE\r\n"
        " SITE JES\r\n"
        " UTF8\r\n"
        "211 End\r\n");
    for (i = 0; i < len; i++)
        buf[i] = ebc2asc[(unsigned char)buf[i]];
    send(sess->ctrl_sock, buf, len, 0);
    return 0;
}

/* --------------------------------------------------------------------
** cmd_help -- HELP: list supported commands
** ----------------------------------------------------------------- */
static int
cmd_help(ftpd_session_t *sess)
{
    ftpd_session_reply_multi(sess, FTP_214,
        "The following commands are recognized:",
        "HELP command successful.");
    return 0;
}

/* --------------------------------------------------------------------
** cmd_noop -- NOOP
** ----------------------------------------------------------------- */
static int
cmd_noop(ftpd_session_t *sess)
{
    ftpd_session_reply(sess, FTP_200, "OK");
    return 0;
}

/* --------------------------------------------------------------------
** cmd_stat -- STAT: server status
** ----------------------------------------------------------------- */
static int
cmd_stat(ftpd_session_t *sess)
{
    ftpd_session_reply(sess, FTP_211, "%s", FTPD_VERSION_STR);
    return 0;
}

/* --------------------------------------------------------------------
** cmd_quit -- QUIT
** ----------------------------------------------------------------- */
static int
cmd_quit(ftpd_session_t *sess)
{
    ftpd_session_reply(sess, FTP_221,
                       "Quit command received. Goodbye.");
    sess->state = SESS_CLOSING;
    return -1;
}

/* --------------------------------------------------------------------
** cmd_type -- TYPE: set transfer type (z/OS response format)
** ----------------------------------------------------------------- */
static int
cmd_type(ftpd_session_t *sess, const char *arg)
{
    char t;
    char first[64];
    char last[64];

    if (!arg[0]) {
        snprintf(last, sizeof(last), "Type remains %s",
                 type_name(sess->type));
        ftpd_session_reply_multi(sess, FTP_501,
            "missing type parameter", last);
        return 0;
    }

    t = (char)toupper((unsigned char)arg[0]);

    if (t == 'A') {
        sess->type = XFER_TYPE_A;
        ftpd_session_reply(sess, FTP_200,
                           "Representation type is Ascii NonPrint");
    }
    else if (t == 'I') {
        sess->type = XFER_TYPE_I;
        ftpd_session_reply(sess, FTP_200,
                           "Representation type is Image");
    }
    else if (t == 'E') {
        /* Check for unsupported format parameter (e.g. TYPE E T) */
        const char *p = arg + 1;
        while (*p == ' ') p++;
        if (*p && toupper((unsigned char)*p) != 'N') {
            snprintf(first, sizeof(first),
                     "TYPE has unsupported format %c",
                     (char)toupper((unsigned char)*p));
            snprintf(last, sizeof(last), "Type remains %s",
                     type_name(sess->type));
            ftpd_session_reply_multi(sess, FTP_504, first, last);
            return 0;
        }
        sess->type = XFER_TYPE_E;
        ftpd_session_reply(sess, FTP_200,
                           "Representation type is Ebcdic NonPrint");
    }
    else if (t == 'L') {
        /* TYPE L 8 is treated as Image (z/OS behavior) */
        const char *p = arg + 1;
        while (*p == ' ') p++;
        if (*p == '8') {
            sess->type = XFER_TYPE_I;
            ftpd_session_reply(sess, FTP_200,
                "Local byte 8, representation type is Image");
        } else {
            snprintf(first, sizeof(first), "unknown type  L %s", p);
            snprintf(last, sizeof(last), "Type remains %s",
                     type_name(sess->type));
            ftpd_session_reply_multi(sess, FTP_501, first, last);
        }
    }
    else {
        snprintf(first, sizeof(first), "unknown type  %c", t);
        snprintf(last, sizeof(last), "Type remains %s",
                 type_name(sess->type));
        ftpd_session_reply_multi(sess, FTP_501, first, last);
    }
    return 0;
}

/* --------------------------------------------------------------------
** cmd_stru -- STRU: set file structure (z/OS response format)
** ----------------------------------------------------------------- */
static int
cmd_stru(ftpd_session_t *sess, const char *arg)
{
    char s = (char)toupper((unsigned char)arg[0]);
    char first[64];
    char last[64];

    if (s == 'F') {
        sess->stru = XFER_STRU_F;
        ftpd_session_reply(sess, FTP_250, "Data structure is File");
    }
    else if (s == 'R') {
        sess->stru = XFER_STRU_R;
        ftpd_session_reply(sess, FTP_250, "Data structure is Record");
    }
    else if (s == 'P') {
        snprintf(last, sizeof(last), "Data structure remains %s",
                 stru_name(sess->stru));
        ftpd_session_reply_multi(sess, FTP_504,
            "Page structure not implemented", last);
    }
    else {
        snprintf(first, sizeof(first), "Unknown structure %c", s);
        snprintf(last, sizeof(last), "Data structure remains %s",
                 stru_name(sess->stru));
        ftpd_session_reply_multi(sess, FTP_504, first, last);
    }
    return 0;
}

/* --------------------------------------------------------------------
** Command dispatch
** ----------------------------------------------------------------- */
int
ftpd_cmd_dispatch(ftpd_session_t *sess, const char *cmd, const char *arg)
{
    /* ----------------------------------------------------------------
    ** Pre-authentication: allow informational commands per z/OS
    ** behavior (ZOS_FTP_REFERENCE.md section 12).
    ** USER, PASS, QUIT, SYST, FEAT, HELP, NOOP, STAT are allowed.
    ** All others require authentication.
    ** ---------------------------------------------------------------- */
    if (!sess->authenticated) {
        if (strcmp(cmd, "USER") == 0) {
            strncpy(sess->user, arg, sizeof(sess->user) - 1);
            sess->state = SESS_AUTH_PASS;
            ftpd_session_reply(sess, FTP_331,
                               "Send password please.");
            return 0;
        }
        if (strcmp(cmd, "PASS") == 0) {
            return ftpd_auth_pass(sess, arg);
        }
        /* Pre-auth allowlist (z/OS compatible) */
        if (strcmp(cmd, "QUIT") == 0) return cmd_quit(sess);
        if (strcmp(cmd, "SYST") == 0) return cmd_syst(sess);
        if (strcmp(cmd, "FEAT") == 0) return cmd_feat(sess);
        if (strcmp(cmd, "HELP") == 0) return cmd_help(sess);
        if (strcmp(cmd, "NOOP") == 0) return cmd_noop(sess);
        if (strcmp(cmd, "STAT") == 0) return cmd_stat(sess);
        if (strcmp(cmd, "AUTH") == 0) {
            ftpd_session_reply(sess, FTP_504,
                "Security mechanism not implemented.");
            return 0;
        }

        ftpd_session_reply(sess, FTP_530, "Not logged in.");
        return 0;
    }

    /* ----------------------------------------------------------------
    ** Authenticated commands
    ** ---------------------------------------------------------------- */

    /* Clear RNFR state on any command other than RNTO */
    if (strcmp(cmd, "RNTO") != 0)
        sess->rnfr_path[0] = '\0';

    if (strcmp(cmd, "QUIT") == 0) return cmd_quit(sess);
    if (strcmp(cmd, "SYST") == 0) return cmd_syst(sess);
    if (strcmp(cmd, "NOOP") == 0) return cmd_noop(sess);
    if (strcmp(cmd, "FEAT") == 0) return cmd_feat(sess);
    if (strcmp(cmd, "HELP") == 0) return cmd_help(sess);
    if (strcmp(cmd, "STAT") == 0) return cmd_stat(sess);
    if (strcmp(cmd, "TYPE") == 0) return cmd_type(sess, arg);
    if (strcmp(cmd, "STRU") == 0) return cmd_stru(sess, arg);

    if (strcmp(cmd, "MODE") == 0) {
        if (arg[0] == 'S' || arg[0] == 's') {
            ftpd_session_reply(sess, FTP_200,
                               "Data transfer mode is Stream");
        } else {
            ftpd_session_reply(sess, FTP_504,
                               "Only stream mode supported");
        }
        return 0;
    }
    if (strcmp(cmd, "PWD") == 0 || strcmp(cmd, "XPWD") == 0) {
        if (sess->fsmode == FS_MVS) {
            if (sess->in_pds) {
                ftpd_session_reply(sess, FTP_257,
                    "\"'%s'\" partitioned data set is working directory.",
                    sess->pds_name);
            } else {
                ftpd_session_reply(sess, FTP_257,
                    "\"'%s'\" is working directory.",
                    sess->mvs_cwd);
            }
        } else {
            ftpd_session_reply(sess, FTP_257,
                               "\"%s\" is the HFS working directory.",
                               sess->ufs_cwd);
        }
        return 0;
    }
    if (strcmp(cmd, "PORT") == 0) {
        if (sess->epsv_all) {
            ftpd_session_reply(sess, FTP_501,
                "PORT not allowed after EPSV ALL");
            return 0;
        }
        if (ftpd_data_port(sess, arg) == 0) {
            ftpd_session_reply(sess, FTP_200, "PORT command successful");
        } else {
            ftpd_session_reply(sess, FTP_501,
                               "Syntax error in PORT parameters");
        }
        return 0;
    }
    if (strcmp(cmd, "PASV") == 0) {
        if (sess->epsv_all) {
            ftpd_session_reply(sess, FTP_501,
                "PASV not allowed after EPSV ALL");
            return 0;
        }
        if (ftpd_data_pasv(sess) != 0) {
            ftpd_session_reply(sess, FTP_425,
                               "Cannot open passive connection");
        }
        return 0;
    }
    if (strcmp(cmd, "EPSV") == 0) {
        /* EPSV ALL — lock session to EPSV-only mode */
        if (arg[0] && (strcmp(arg, "ALL") == 0 ||
                       strcmp(arg, "all") == 0)) {
            sess->epsv_all = 1;
            ftpd_session_reply(sess, FTP_200,
                "EPSV ALL command successful.");
            return 0;
        }
        if (ftpd_data_epsv(sess) != 0) {
            ftpd_session_reply(sess, FTP_425,
                               "Cannot open passive connection");
        }
        return 0;
    }
    if (strcmp(cmd, "EPRT") == 0) {
        ftpd_session_reply(sess, FTP_522,
            "Network protocol not supported, use (1)");
        return 0;
    }
    if (strcmp(cmd, "ABOR") == 0) {
        ftpd_data_close(sess);
        ftpd_session_reply(sess, FTP_226, "Abort successful");
        return 0;
    }
    if (strcmp(cmd, "CWD") == 0 || strcmp(cmd, "XCWD") == 0) {
        // Mode switching: CWD /... → UFS, CWD 'DSN' → MVS
        if (arg[0] == '/') {
            // Try UFS — only switch mode if ftpd_ufs_get() succeeds
            // (prevents getting stuck in UFS mode when UFSD is down)
            int prev = sess->fsmode;
            sess->fsmode = FS_UFS;
            if (!ftpd_ufs_get(sess)) {
                sess->fsmode = prev;  // UFSD not available, stay
                return 0;            // 550 already sent
            }
            return ftpd_ufs_cwd(sess, arg);
        }
        if (sess->fsmode == FS_UFS) {
            // Quoted name while in UFS → switch back to MVS
            if (arg[0] == '\'') {
                sess->fsmode = FS_MVS;
                return ftpd_mvs_cwd(sess, arg);
            }
            // Relative navigation within UFS
            return ftpd_ufs_cwd(sess, arg);
        }
        return ftpd_mvs_cwd(sess, arg);
    }
    if (strcmp(cmd, "LIST") == 0) {
        if (sess->filetype == FT_JES)
            return ftpd_jes_list(sess, arg);
        if (sess->fsmode == FS_UFS)
            return ftpd_ufs_list(sess, arg, 0);
        return ftpd_mvs_list(sess, arg, 0);
    }
    if (strcmp(cmd, "NLST") == 0) {
        if (sess->filetype == FT_JES)
            return ftpd_jes_list(sess, arg);
        if (sess->fsmode == FS_UFS)
            return ftpd_ufs_list(sess, arg, 1);
        return ftpd_mvs_list(sess, arg, 1);
    }
    if (strcmp(cmd, "SIZE") == 0) {
        if (sess->fsmode == FS_UFS)
            return ftpd_ufs_size(sess, arg);
        {
            long sz = ftpd_mvs_size(sess, arg);
            if (sz >= 0)
                ftpd_session_reply(sess, FTP_213, "%ld", sz);
            else
                ftpd_session_reply(sess, FTP_550, "Dataset not found");
            return 0;
        }
    }
    if (strcmp(cmd, "MDTM") == 0) {
        if (sess->fsmode == FS_UFS)
            return ftpd_ufs_mdtm(sess, arg);
        ftpd_session_reply(sess, FTP_502, "MDTM not implemented for MVS");
        return 0;
    }
    if (strcmp(cmd, "CDUP") == 0 || strcmp(cmd, "XCUP") == 0) {
        if (sess->fsmode == FS_UFS)
            return ftpd_ufs_cdup(sess);
        return ftpd_mvs_cdup(sess);
    }
    if (strcmp(cmd, "RETR") == 0) {
        if (sess->filetype == FT_JES)
            return ftpd_jes_retrieve(sess, arg);
        if (sess->fsmode == FS_UFS)
            return ftpd_ufs_retr(sess, arg);
        return ftpd_mvs_retr(sess, arg);
    }
    if (strcmp(cmd, "STOR") == 0) {
        if (sess->filetype == FT_JES)
            return ftpd_jes_submit(sess);
        if (sess->fsmode == FS_UFS)
            return ftpd_ufs_stor(sess, arg);
        return ftpd_mvs_stor(sess, arg);
    }
    if (strcmp(cmd, "APPE") == 0) {
        if (sess->fsmode == FS_UFS) {
            ftpd_session_reply(sess, FTP_502,
                               "APPE not supported for UFS");
            return 0;
        }
        return ftpd_mvs_appe(sess, arg);
    }
    if (strcmp(cmd, "DELE") == 0) {
        if (sess->filetype == FT_JES)
            return ftpd_jes_delete(sess, arg);
        if (sess->fsmode == FS_UFS)
            return ftpd_ufs_dele(sess, arg);
        return ftpd_mvs_dele(sess, arg);
    }
    if (strcmp(cmd, "MKD") == 0 || strcmp(cmd, "XMKD") == 0) {
        if (sess->fsmode == FS_UFS)
            return ftpd_ufs_mkd(sess, arg);
        return ftpd_mvs_mkd(sess, arg);
    }
    if (strcmp(cmd, "RMD") == 0 || strcmp(cmd, "XRMD") == 0) {
        if (sess->fsmode == FS_UFS)
            return ftpd_ufs_rmd(sess, arg);
        return ftpd_mvs_rmd(sess, arg);
    }
    if (strcmp(cmd, "RNFR") == 0) {
        if (sess->fsmode == FS_UFS)
            return ftpd_ufs_rnfr(sess, arg);
        return ftpd_mvs_rnfr(sess, arg);
    }
    if (strcmp(cmd, "RNTO") == 0) {
        if (sess->fsmode == FS_UFS)
            return ftpd_ufs_rnto(sess, arg);
        return ftpd_mvs_rnto(sess, arg);
    }
    if (strcmp(cmd, "SITE") == 0) {
        return ftpd_site_dispatch(sess, arg);
    }
    if (strcmp(cmd, "OPTS") == 0) {
        ftpd_session_reply(sess, FTP_202,
            "UTF8 mode is always enabled.");
        return 0;
    }
    if (strcmp(cmd, "AUTH") == 0) {
        ftpd_session_reply(sess, FTP_504,
            "Security mechanism not implemented.");
        return 0;
    }

    /* Unknown command */
    ftpd_session_reply(sess, FTP_500, "unknown command %s", cmd);
    return 0;
}
