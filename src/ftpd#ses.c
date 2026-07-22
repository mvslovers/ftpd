/*
** FTPD Session Handler
**
** Per-connection state machine and thread lifecycle.
** Each FTP client runs in its own thdmgr worker thread.
*/
#include "ftpd.h"
#include "ftpd#ses.h"
#include "ftpd#cmd.h"
#include "ftpd#dat.h"
#include "ftpd#ufs.h"
#include "libufs.h"                 /* UFSFILE, ufs_fclose()          */
#include "cliblock.h"               /* unlock() — release orphaned ENQ */

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
    sess->prev_fsmode = FS_MVS;
    sess->type = XFER_TYPE_A;
    sess->stru = XFER_STRU_F;
    sess->authenticated = 0;
    sess->auth_attempts = 0;
    sess->acee = NULL;
    sess->rest_offset = 0;
    sess->ufs = NULL;
    strcpy(sess->ufs_cwd, "/");
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

    ftpd_ufs_free(sess);

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

    /* Ensure CRLF termination */
    buf[len++] = '\r';
    buf[len++] = '\n';

    /* EBCDIC -> ASCII */
    for (i = 0; i < len; i++)
        buf[i] = ebc2asc[(unsigned char)buf[i]];

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

    /* First line: "code-text\r\n" */
    len = snprintf(buf, sizeof(buf) - 2, "%d-%s\r\n", code, first);
    for (i = 0; i < len; i++)
        buf[i] = ebc2asc[(unsigned char)buf[i]];
    send(sess->ctrl_sock, buf, len, 0);

    /* Last line: "code text\r\n" */
    len = snprintf(buf, sizeof(buf) - 2, "%d %s\r\n", code, last);
    for (i = 0; i < len; i++)
        buf[i] = ebc2asc[(unsigned char)buf[i]];
    send(sess->ctrl_sock, buf, len, 0);
}

/* --------------------------------------------------------------------
** Read one command line from control connection.
** Reads byte-by-byte, converts ASCII -> EBCDIC.
** Returns command length, or -1 on error/disconnect.
**
** Uses short select() timeout (5 seconds) and checks FTPD_QUIESCE
** on every timeout — matching HTTPD's ftpcgets.c pattern.  The real
** idle timeout is measured cumulatively across multiple 5s rounds.
** Without this, a 300s select() blocks shutdown for up to 5 minutes
** because cthread_manager_term() waits for all workers to exit.
** ----------------------------------------------------------------- */
int
ftpd_session_getline(ftpd_session_t *sess)
{
    unsigned char c;
    int rc;
    int idle_timeout;
    time_t now;
    fd_set rfds;
    struct timeval tv;

    sess->cmdlen = 0;
    memset(sess->cmd, 0, sizeof(sess->cmd));

    idle_timeout = sess->server->config.idle_timeout;
    if (idle_timeout <= 0)
        idle_timeout = 300;

    /* Idle start lives in the session struct (heap) — immune to
    ** stack corruption caused by select()/SVC 75.
    */
    sess->idle_start = time(NULL);

    while (sess->cmdlen < FTPD_MAX_CMD_LEN - 1) {
        /* Short select timeout — poll every 5 seconds so we can
        ** check the shutdown flag.  The real idle timeout is
        ** measured cumulatively via idle_start/now.
        */
        FD_ZERO(&rfds);
        FD_SET(sess->ctrl_sock, &rfds);
        tv.tv_sec = 5;
        tv.tv_usec = 0;

        rc = select(sess->ctrl_sock + 1, &rfds, NULL, NULL, &tv);
        if (rc < 0)
            return -1;

        if (rc == 0) {
            /* select() timed out — check shutdown and idle */
            if (sess->server->flags & FTPD_QUIESCE)
                return -1;

            now = time(NULL);
            if (now > sess->idle_start &&
                (now - sess->idle_start) >= (time_t)idle_timeout) {
                ftpd_session_reply(sess, FTP_421,
                                   "Idle timeout, closing connection");
                return -1;
            }
            continue;
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
** try()-wrapped command dispatch.
**
** try() returns the ABEND code (0 on clean completion) and discards the
** wrapped function's own return value, so ftpd_cmd_dispatch()'s rc — which
** signals the command loop to exit (QUIT, auth failure, fatal I/O) — is
** stashed in the session and read by the caller after try() returns.
** ----------------------------------------------------------------- */
static int
ftpd_run_command(ftpd_session_t *sess, const char *cmd, const char *arg)
{
    sess->dispatch_rc = ftpd_cmd_dispatch(sess, cmd, arg);
    return 0;
}

/* --------------------------------------------------------------------
** Per-command ABEND recovery.
**
** Runs after try() catches an ABEND in ftpd_run_command().  crent370's
** try() is SDWA-retry that unwinds the stack back to the try() frame and
** resumes in normal task mode, so this handler runs on a valid stack and
** may use ordinary services (racf_set_acee, fclose, send, WTO).  The
** session struct is heap-allocated and survives; only stack frames below
** try() are gone.
**
** This function is itself run under try() by the caller, so a re-ABEND in
** a cleanup step is contained.  Ordering is therefore deliberate: restore
** identity and release the ASXB ENQ first (so they hold even on a re-
** ABEND); WTO + tell the client next; and only then perform the one step
** that can plausibly re-ABEND (fclose on a possibly-corrupt DCB), so a
** cleanup failure can never leave the client hanging.
**
** 'verb' is the parsed FTP command word only (no arguments) — never the
** raw command line, which for PASS would contain the password.
** ----------------------------------------------------------------- */
static int
ftpd_session_recover(ftpd_session_t *sess, unsigned abcode, const char *verb)
{
    FILE *fp;

    /* 1. Reset the address-space-wide ASXBSENV to the STC identity.  If
    ** the ABEND struck while switched to the user's ACEE (inside a
    ** RETR/STOR/LIST OPEN), the shared field still points at the user —
    ** possibly at an ACEE that is about to be freed.  Restoring the STC
    ** identity re-establishes the AS's normal resting state and is fail-
    ** closed: FTPD/USER is least-privilege, so any concurrent session
    ** transiently pulled onto it can only lose authority, never gain it. */
    racf_set_acee(sess->server->stc_acee);

    /* 2. Release the ASXB ENQ if this task ABENDed inside racf_auth()'s
    ** lock(asxb)/unlock(asxb) critical section.  MVS DEQs a task's ENQs
    ** at task termination, but recovery keeps the task alive, so an
    ** orphaned AS-wide ENQ would stall every session's racf_auth() until
    ** this worker's next command.  unlock() is safe when not held. */
    {
        unsigned *psa  = (unsigned *)0;
        unsigned *ascb = (unsigned *)psa[0x224/4];  /* PSAAOLD  -> ASCB */
        unsigned *asxb = (unsigned *)ascb[0x6C/4];  /* ASCBASXB -> ASXB */
        unlock(asxb, 0);
    }

    /* 3. Count + WTO now, before the risky cleanup, so the diagnostic
    ** survives even a cleanup re-ABEND.  total_recover accumulates over
    ** the STC lifetime (per-session growth is bounded by FTPD_MAX_RECOVER)
    ** so leaked-resource accumulation from the documented residual windows
    ** stays observable in the operator log. */
    sess->server->total_recover++;
    if (abcode == 0)
        ftpd_log_wto("FTPD070E ABEND recovery (ESTAE create failed) "
                     "cmd=%s socket=%d total=%u",
                     verb, sess->ctrl_sock, sess->server->total_recover);
    else if (abcode > 0xFFF)
        ftpd_log_wto("FTPD070E ABEND S%03X recovered cmd=%s socket=%d "
                     "total=%u", (abcode >> 12) & 0xFFF, verb,
                     sess->ctrl_sock, sess->server->total_recover);
    else
        ftpd_log_wto("FTPD070E ABEND U%04u recovered cmd=%s socket=%d "
                     "total=%u", abcode, verb, sess->ctrl_sock,
                     sess->server->total_recover);

    /* 4. Tell the client before touching in-flight resources, so a
    ** re-ABEND in cleanup cannot leave it hanging until timeout. */
    ftpd_session_reply(sess, FTP_451,
        "Requested action aborted: local error in processing.");

    /* 5. Release the in-flight transfer handle.  Clear the tracking field
    ** BEFORE closing so a re-ABEND in the close (contained by the caller's
    ** try()) can never cause a double close on a subsequent recovery.  At
    ** most one of these is set (a session is in MVS or UFS mode, one
    ** transfer at a time).  MVS fclose releases the DCB + fopen's SVC 99
    ** DD; UFS ufs_fclose releases the cross-AS UFSD file. */
    fp = sess->cur_file;
    sess->cur_file = NULL;
    if (fp)
        fclose(fp);

    if (sess->cur_ufs_file) {
        UFSFILE *uf = sess->cur_ufs_file;
        sess->cur_ufs_file = NULL;
        ufs_fclose(&uf);
    }

    ftpd_data_close(sess);

    return 0;
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
    int try_rc;
    int recover_count;
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

        /* Consecutive per-command ABEND recoveries; reset on any clean
        ** command so the guard only trips on a wedged session. */
        recover_count = 0;

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

            /* Dispatch under ESTAE recovery.  try() returns the ABEND
            ** code (0 = clean); the handler's real rc is stashed in
            ** sess->dispatch_rc.  A negative try_rc means the ESTAE could
            ** not be created (the command did not run); treat it like a
            ** recovery so the client is answered and the guard advances. */
            sess->dispatch_rc = 0;
            try_rc = try(ftpd_run_command, sess, cmd, arg);

            if (try_rc != 0) {
                unsigned abcode = (try_rc > 0) ? (unsigned)try_rc : 0;

                recover_count++;

                /* Contain a re-ABEND during recovery itself. */
                try(ftpd_session_recover, sess, abcode, cmd);

                /* A session that ABENDs every command is wedged (e.g.
                ** corrupt state); stop recovering and close it cleanly.
                ** The worker survives and serves the next connection. */
                if (recover_count >= FTPD_MAX_RECOVER) {
                    ftpd_log_wto("FTPD071E session socket=%d closed after "
                                 "%d consecutive ABENDs", sess->ctrl_sock,
                                 recover_count);
                    break;
                }
                continue;
            }

            recover_count = 0;

            if (sess->dispatch_rc < 0)
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
