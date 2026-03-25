/*
** FTPD Session Handler
**
** Per-connection state machine and thread lifecycle.
** Each FTP client runs in its own thdmgr worker thread.
*/
#include "ftpd.h"
#include "ftpd#ses.h"
#include "ftpd#cmd.h"

/* --------------------------------------------------------------------
** Allocate and initialize a new session
** ----------------------------------------------------------------- */
ftpd_session_t *
ftpd_session_new(ftpd_server_t *server, int sock)
{
    ftpd_session_t *sess;

    sess = calloc(1, sizeof(ftpd_session_t));
    if (!sess) {
        ftpd_log(LOG_ERROR, "%s: calloc failed", __func__);
        return NULL;
    }

    strcpy(sess->eye, FTPD_SES_EYE);
    sess->ctrl_sock = sock;
    sess->data_sock = -1;
    sess->pasv_sock = -1;
    sess->data_mode = DATA_NONE;
    sess->state = SESS_GREETING;
    sess->filetype = FT_SEQ;
    sess->fsmode = FS_MVS;
    sess->type = XFER_TYPE_A;
    sess->stru = XFER_STRU_F;
    sess->authenticated = 0;
    sess->auth_attempts = 0;
    sess->acee = NULL;
    sess->rest_offset = 0;
    sess->server = server;

    /* Set default allocation from server config */
    strcpy(sess->alloc.recfm, server->config.defaults.recfm);
    sess->alloc.lrecl = server->config.defaults.lrecl;
    sess->alloc.blksize = server->config.defaults.blksize;
    sess->alloc.primary = 10;
    sess->alloc.secondary = 5;
    strcpy(sess->alloc.spacetype, "TRK");
    strcpy(sess->alloc.volume, server->config.defaults.volume);
    strcpy(sess->alloc.unit, server->config.defaults.unit);
    sess->alloc.dirblks = 0;

    /* JES defaults from config */
    sess->jes_level = server->config.jes_level;

    return sess;
}

/* --------------------------------------------------------------------
** Free session resources
** ----------------------------------------------------------------- */
void
ftpd_session_free(ftpd_session_t *sess)
{
    if (!sess)
        return;

    if (sess->data_sock >= 0)
        closesocket(sess->data_sock);
    if (sess->pasv_sock >= 0)
        closesocket(sess->pasv_sock);
    if (sess->ctrl_sock >= 0)
        closesocket(sess->ctrl_sock);

    if (sess->acee)
        racf_logout(&sess->acee);

    free(sess);
}

/* --------------------------------------------------------------------
** Send FTP reply on control connection (EBCDIC -> ASCII)
** ----------------------------------------------------------------- */
void
ftpd_session_reply(ftpd_session_t *sess, int code, const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    int len;
    int i;

    /* Format: "code message\r\n" */
    len = snprintf(buf, sizeof(buf) - 2, "%d ", code);

    va_start(ap, fmt);
    len += vsnprintf(buf + len, sizeof(buf) - len - 2, fmt, ap);
    va_end(ap);

    /* EBCDIC -> ASCII (message text only, not the CRLF) */
    for (i = 0; i < len; i++)
        buf[i] = ebc2asc[(unsigned char)buf[i]];

    /* Append ASCII CRLF directly — do NOT use '\r'/'\n' through the
    ** translation table: c2asm370 maps '\n' to EBCDIC 0x15 (NEL),
    ** and ebc2asc[0x15] = 0x85 (ASCII NEL), not 0x0A (LF).
    ** FTP requires ASCII CR LF (0x0D 0x0A).
    */
    buf[len++] = 0x0D;     /* ASCII CR */
    buf[len++] = 0x0A;     /* ASCII LF */

    send(sess->ctrl_sock, buf, len, 0);

    ftpd_trace(">>> %d %.*s", code, len - 2, buf + 4);
}

/* --------------------------------------------------------------------
** Send multi-line FTP reply
** ----------------------------------------------------------------- */
void
ftpd_session_reply_multi(ftpd_session_t *sess, int code,
                         const char *first, const char *last)
{
    char buf[512];
    int len;
    int i;

    /* First line: "code-text" + ASCII CRLF */
    len = snprintf(buf, sizeof(buf) - 2, "%d-%s", code, first);
    for (i = 0; i < len; i++)
        buf[i] = ebc2asc[(unsigned char)buf[i]];
    buf[len++] = 0x0D;
    buf[len++] = 0x0A;
    send(sess->ctrl_sock, buf, len, 0);

    /* Last line: "code text" + ASCII CRLF */
    len = snprintf(buf, sizeof(buf) - 2, "%d %s", code, last);
    for (i = 0; i < len; i++)
        buf[i] = ebc2asc[(unsigned char)buf[i]];
    buf[len++] = 0x0D;
    buf[len++] = 0x0A;
    send(sess->ctrl_sock, buf, len, 0);
}

/* --------------------------------------------------------------------
** Read one command line from control connection.
** Reads byte-by-byte, converts ASCII -> EBCDIC.
** Returns command length, or -1 on error/disconnect.
** ----------------------------------------------------------------- */
int
ftpd_session_getline(ftpd_session_t *sess)
{
    unsigned char c;
    int rc;
    fd_set rfds;
    struct timeval tv;

    sess->cmdlen = 0;
    memset(sess->cmd, 0, sizeof(sess->cmd));

    while (sess->cmdlen < FTPD_MAX_CMD_LEN - 1) {
        /* Wait for data with timeout */
        FD_ZERO(&rfds);
        FD_SET(sess->ctrl_sock, &rfds);
        tv.tv_sec = sess->server->config.idle_timeout;
        tv.tv_usec = 0;

        rc = select(sess->ctrl_sock + 1, &rfds, NULL, NULL, &tv);
        if (rc <= 0) {
            if (rc == 0) {
                ftpd_session_reply(sess, FTP_421,
                                   "Idle timeout, closing connection");
            }
            return -1;
        }

        rc = recv(sess->ctrl_sock, &c, 1, 0);
        if (rc <= 0)
            return -1;

        /* Skip CR */
        if (c == 0x0D)
            continue;

        /* LF signals end of command */
        if (c == 0x0A) {
            sess->cmd[sess->cmdlen] = '\0';
            ftpd_trace("<<< %s", sess->cmd);
            return sess->cmdlen;
        }

        /* ASCII -> EBCDIC */
        sess->cmd[sess->cmdlen++] = asc2ebc[c];
    }

    /* Command too long */
    sess->cmd[sess->cmdlen] = '\0';
    return sess->cmdlen;
}

/* --------------------------------------------------------------------
** Main session loop -- thdmgr worker thread entry point.
**
** This function is called by cthread_worker_wait() with the session
** data pointer. It runs the full FTP session until QUIT or error.
** ----------------------------------------------------------------- */
int
ftpd_session_run(void *udata, CTHDWORK *work)
{
    ftpd_server_t *server = (ftpd_server_t *)udata;
    ftpd_session_t *sess = NULL;
    char *data = NULL;
    int rc;
    char cmd[8];
    char *arg;
    char *p;
    int i;

    for (;;) {
        rc = cthread_worker_wait(work, &data);

        if (rc == CTHDWORK_POST_SHUTDOWN)
            break;

        if (rc != CTHDWORK_POST_REQUEST || !data)
            continue;

        sess = (ftpd_session_t *)data;

        /* Verify eye catcher */
        if (strcmp(sess->eye, FTPD_SES_EYE) != 0) {
            ftpd_log(LOG_ERROR, "%s: invalid session eye catcher", __func__);
            continue;
        }

        server->num_sessions++;
        server->total_sessions++;

        ftpd_log(LOG_INFO, "%s: session started, socket %d", __func__,
                 sess->ctrl_sock);

        /* Send 220 greeting */
        ftpd_session_reply(sess, FTP_220, "%s", server->config.banner);
        sess->state = SESS_AUTH_USER;

        /* Command loop */
        while (sess->state != SESS_CLOSING) {
            if (work->state == CTHDWORK_STATE_SHUTDOWN)
                break;

            rc = ftpd_session_getline(sess);
            if (rc < 0) {
                ftpd_log(LOG_INFO, "%s: session disconnect, socket %d",
                         __func__, sess->ctrl_sock);
                break;
            }
            if (rc == 0)
                continue;

            /* Parse command: first word (up to 4 chars) + argument */
            p = sess->cmd;
            i = 0;
            while (*p && *p != ' ' && i < (int)sizeof(cmd) - 1) {
                /* Uppercase the command */
                cmd[i] = (char)toupper((unsigned char)*p);
                i++;
                p++;
            }
            cmd[i] = '\0';

            /* Skip spaces to find argument */
            while (*p == ' ')
                p++;
            arg = p;

            /* Dispatch */
            rc = ftpd_cmd_dispatch(sess, cmd, arg);
            if (rc < 0)
                break;
        }

        /* Cleanup */
        ftpd_log(LOG_INFO, "%s: session ended, user=%s sent=%ld recv=%ld "
                 "xfers=%d", __func__,
                 sess->user[0] ? sess->user : "(none)",
                 sess->bytes_sent, sess->bytes_recv, sess->xfer_count);

        server->total_bytes_in += sess->bytes_recv;
        server->total_bytes_out += sess->bytes_sent;
        server->num_sessions--;

        ftpd_session_free(sess);
        sess = NULL;
    }

    return 0;
}
