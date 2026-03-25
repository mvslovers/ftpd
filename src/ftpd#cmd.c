/*
** FTPD Command Parser & Dispatcher
**
** FTP protocol command handling (client-facing).
** Stub implementation for Step 1.2.
** Full implementation in Step 1.3.
*/
#include "ftpd.h"
#include "ftpd#ses.h"
#include "ftpd#cmd.h"
#include "ftpd#dat.h"

/* --------------------------------------------------------------------
** Command dispatch -- minimal set for Step 1.2 testing
** ----------------------------------------------------------------- */
int
ftpd_cmd_dispatch(ftpd_session_t *sess, const char *cmd, const char *arg)
{
    /* Before authentication: only USER, PASS, QUIT, HELP */
    if (!sess->authenticated) {
        if (strcmp(cmd, "USER") == 0) {
            strncpy(sess->user, arg, sizeof(sess->user) - 1);
            sess->state = SESS_AUTH_PASS;
            ftpd_session_reply(sess, FTP_331,
                               "User name okay, need password");
            return 0;
        }
        if (strcmp(cmd, "PASS") == 0) {
            /* Stub: accept any password for now */
            sess->authenticated = 1;
            strcpy(sess->hlq, sess->user);
            strcat(sess->hlq, ".");
            strcpy(sess->mvs_cwd, sess->hlq);
            sess->state = SESS_READY;
            ftpd_session_reply(sess, FTP_230, "User %s logged in",
                               sess->user);
            ftpd_log(LOG_INFO, "%s: User %s logged in", __func__,
                     sess->user);
            return 0;
        }
        if (strcmp(cmd, "QUIT") == 0) {
            ftpd_session_reply(sess, FTP_221, "Goodbye");
            sess->state = SESS_CLOSING;
            return -1;
        }
        ftpd_session_reply(sess, FTP_530, "Not logged in");
        return 0;
    }

    /* Authenticated commands */
    if (strcmp(cmd, "QUIT") == 0) {
        ftpd_session_reply(sess, FTP_221, "Goodbye");
        sess->state = SESS_CLOSING;
        return -1;
    }
    if (strcmp(cmd, "SYST") == 0) {
        ftpd_session_reply(sess, FTP_215,
                           "MVS is the operating system");
        return 0;
    }
    if (strcmp(cmd, "NOOP") == 0) {
        ftpd_session_reply(sess, FTP_200, "Command okay");
        return 0;
    }
    if (strcmp(cmd, "TYPE") == 0) {
        if (arg[0] == 'A' || arg[0] == 'a') {
            sess->type = XFER_TYPE_A;
            ftpd_session_reply(sess, FTP_200, "Type set to A");
        }
        else if (arg[0] == 'I' || arg[0] == 'i') {
            sess->type = XFER_TYPE_I;
            ftpd_session_reply(sess, FTP_200, "Type set to I");
        }
        else if (arg[0] == 'E' || arg[0] == 'e') {
            sess->type = XFER_TYPE_E;
            ftpd_session_reply(sess, FTP_200, "Type set to E");
        }
        else {
            ftpd_session_reply(sess, FTP_504,
                               "Type not implemented for that parameter");
        }
        return 0;
    }
    if (strcmp(cmd, "MODE") == 0) {
        if (arg[0] == 'S' || arg[0] == 's') {
            ftpd_session_reply(sess, FTP_200, "Mode set to S");
        } else {
            ftpd_session_reply(sess, FTP_504,
                               "Only stream mode supported");
        }
        return 0;
    }
    if (strcmp(cmd, "STRU") == 0) {
        if (arg[0] == 'F' || arg[0] == 'f') {
            sess->stru = XFER_STRU_F;
            ftpd_session_reply(sess, FTP_200, "Structure set to F");
        }
        else if (arg[0] == 'R' || arg[0] == 'r') {
            sess->stru = XFER_STRU_R;
            ftpd_session_reply(sess, FTP_200, "Structure set to R");
        }
        else {
            ftpd_session_reply(sess, FTP_504,
                               "Structure not implemented for that parameter");
        }
        return 0;
    }
    if (strcmp(cmd, "PWD") == 0 || strcmp(cmd, "XPWD") == 0) {
        if (sess->fsmode == FS_MVS) {
            ftpd_session_reply(sess, FTP_257,
                               "\"'%s'\" is working directory",
                               sess->mvs_cwd);
        } else {
            ftpd_session_reply(sess, FTP_257,
                               "\"%s\" is working directory",
                               sess->ufs_cwd);
        }
        return 0;
    }
    if (strcmp(cmd, "PORT") == 0) {
        if (ftpd_data_port(sess, arg) == 0) {
            ftpd_session_reply(sess, FTP_200, "PORT command successful");
        } else {
            ftpd_session_reply(sess, FTP_501,
                               "Syntax error in PORT parameters");
        }
        return 0;
    }
    if (strcmp(cmd, "PASV") == 0) {
        if (ftpd_data_pasv(sess) != 0) {
            ftpd_session_reply(sess, FTP_425,
                               "Cannot open passive connection");
        }
        return 0;
    }
    if (strcmp(cmd, "FEAT") == 0) {
        char buf[256];
        int len, i;
        len = snprintf(buf, sizeof(buf),
            "211-Features supported\r\n"
            " SIZE\r\n"
            " MDTM\r\n"
            " REST STREAM\r\n"
            " SITE FILETYPE\r\n"
            " SITE JES\r\n"
            " UTF8\r\n"
            "211 End\r\n");
        for (i = 0; i < len; i++)
            buf[i] = ebc2asc[(unsigned char)buf[i]];
        send(sess->ctrl_sock, buf, len, 0);
        return 0;
    }
    if (strcmp(cmd, "HELP") == 0) {
        ftpd_session_reply_multi(sess, FTP_214,
            "The following commands are recognized:",
            "HELP command complete");
        return 0;
    }
    if (strcmp(cmd, "STAT") == 0) {
        ftpd_session_reply(sess, FTP_211, "%s", FTPD_VERSION_STR);
        return 0;
    }
    if (strcmp(cmd, "ABOR") == 0) {
        ftpd_data_close(sess);
        ftpd_session_reply(sess, FTP_226, "Abort successful");
        return 0;
    }
    if (strcmp(cmd, "LIST") == 0 || strcmp(cmd, "NLST") == 0) {
        /* Stub: return empty listing */
        ftpd_session_reply(sess, FTP_150,
                           "Opening data connection for file list");
        if (ftpd_data_open(sess) == 0) {
            ftpd_data_close(sess);
            ftpd_session_reply(sess, FTP_226,
                               "Transfer complete");
        } else {
            ftpd_session_reply(sess, FTP_425,
                               "Cannot open data connection");
        }
        return 0;
    }

    /* Unknown command */
    ftpd_session_reply(sess, FTP_500,
                       "Syntax error, unrecognized command");
    return 0;
}
