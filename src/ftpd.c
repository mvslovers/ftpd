/*
** FTPD - Standalone FTP Server for MVS 3.8j
**
** Main entry point: socket listener, accept loop, console command
** handler (CIB processing), and graceful shutdown.
**
** Operator interface follows the UFSD pattern:
**   - Unconditional CIB drain before WAIT
**   - Flat MODIFY commands: STATS, SESSIONS, CONFIG, VERSION, TRACE, HELP
**   - Flag-based shutdown (FTPD_ACTIVE / FTPD_QUIESCE)
*/
#include "ftpd.h"
#include "ftpd#ses.h"
#include "clibppa.h"
#include "clibos.h"

/* Global server state */
ftpd_server_t *ftpd_server = NULL;

/* Forward declarations */
static int  socket_thread(void *arg1, void *arg2);
static int  initialize(ftpd_server_t *server, int argc, char **argv);
static void terminate(ftpd_server_t *server);

/* ====================================================================
** main() -- Server entry point
** ================================================================= */
int
main(int argc, char **argv)
{
    ftpd_server_t server;
    COM *com;
    CIB *cib;
    int rc;

    memset(&server, 0, sizeof(server));
    strcpy(server.eye, FTPD_EYE);
    ftpd_server = &server;

    /* Initialize server */
    rc = initialize(&server, argc, argv);
    if (rc != 0) {
        ftpd_log_wto("FTPD099E Initialization failed, rc=%d", rc);
        return rc;
    }

    server.flags |= FTPD_ACTIVE;

    ftpd_log_wto("FTPD001I FTPD %s started on port %d",
                 FTPD_VERSION, server.config.port);

    /* Get COM area for console interface */
    com = __gtcom();
    if (!com) {
        ftpd_log(LOG_ERROR, "%s: COM area not available", __func__);
        terminate(&server);
        return 8;
    }

    /* Accept and delete the start CIB */
    cib = __cibget();
    if (cib) {
        if (cib->cibverb == CIBSTART)
            __cibdel(cib);
    }

    /* ----------------------------------------------------------------
    ** Main event loop (UFSD pattern)
    **
    ** 1. Drain ALL pending CIBs unconditionally
    ** 2. STIMER WAIT (console ECB + periodic wakeup)
    ** ---------------------------------------------------------------- */
    while (server.flags & FTPD_ACTIVE) {

        /* Drain all pending CIBs unconditionally */
        while ((cib = __cibget()) != NULL) {
            ftpd_process_cib(&server, cib);
            __cibdel(cib);
        }

        /* Wait for next console event (1 second timeout) */
        __asm__("STIMER WAIT,BINTVL==F'100'   1 second");
    }

    terminate(&server);

    ftpd_log_wto("FTPD098I FTPD shutdown complete");

    return 0;
}

/* ====================================================================
** Initialize -- config, logging, trace, socket thread, worker pool
** ================================================================= */
static int
initialize(ftpd_server_t *server, int argc, char **argv)
{
    const char *cfg_dsn = NULL;
    int i;
    int rc;

    /* APF authorize STEPLIB */
    rc = clib_apf_setup(argv[0]);
    if (rc) {
        ftpd_log_wto("FTPD003W APF setup failed RC=%d", rc);
    } else {
        ftpd_log_wto("FTPD003I STEPLIB APF authorized");
    }

    /* Parse PARM for config override: CONFIG=dsname */
    for (i = 1; i < argc; i++) {
        if (strncmp(argv[i], "CONFIG=", 7) == 0) {
            cfg_dsn = argv[i] + 7;
        }
    }

    /* Initialize trace ring buffer */
    ftpd_trace_init(512);

    /* Load configuration */
    if (ftpdcfg_load(&server->config, cfg_dsn) != 0) {
        return 4;
    }

    /* Create socket thread (handles listener + accept loop) */
    server->listen_sock = -1;
    cthread_create_ex(socket_thread, server, NULL, 32 * 1024);

    /* Create worker thread pool */
    server->mgr = cthread_manager_init(
        server->config.max_sessions,
        ftpd_session_run,
        server,
        64 * 1024
    );

    if (!server->mgr) {
        ftpd_log(LOG_ERROR, "%s: failed to create thread manager", __func__);
        return 8;
    }

    /* Post to manager to start dispatcher */
    cthread_post(&server->mgr->wait, CTHDMGR_POST_DATA);

    return 0;
}

/* ====================================================================
** Socket thread -- listener, accept loop
** ================================================================= */
static int
socket_thread(void *arg1, void *arg2)
{
    ftpd_server_t *server = (ftpd_server_t *)arg1;
    struct sockaddr_in saddr;
    struct sockaddr_in caddr;
    int len;
    int sock;
    fd_set rfds;
    struct timeval tv;
    int rc;
    unsigned a1, a2, a3, a4;
    ftpd_session_t *sess;

    (void)arg2;

    /* Create listening socket */
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        ftpd_log(LOG_ERROR, "%s: socket() failed, errno=%d", __func__, errno);
        server->flags &= ~FTPD_ACTIVE;
        return 8;
    }

    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(server->config.port);

    if (strcmp(server->config.bind_ip, "ANY") == 0) {
        saddr.sin_addr.s_addr = 0;
    } else {
        if (sscanf(server->config.bind_ip, "%u.%u.%u.%u",
                   &a1, &a2, &a3, &a4) == 4) {
            saddr.sin_addr.s_addr = htonl(
                (a1 << 24) | (a2 << 16) | (a3 << 8) | a4);
        }
    }

    if (bind(sock, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
        ftpd_log(LOG_ERROR, "%s: bind() failed on port %d, errno=%d",
                 __func__, server->config.port, errno);
        closesocket(sock);
        server->flags &= ~FTPD_ACTIVE;
        return 8;
    }

    if (listen(sock, 10) < 0) {
        ftpd_log(LOG_ERROR, "%s: listen() failed, errno=%d", __func__, errno);
        closesocket(sock);
        server->flags &= ~FTPD_ACTIVE;
        return 8;
    }

    server->listen_sock = sock;
    ftpd_log(LOG_INFO, "%s: listening on port %d", __func__,
             server->config.port);

    /* Accept loop */
    while (server->flags & FTPD_ACTIVE) {
        FD_ZERO(&rfds);
        FD_SET(sock, &rfds);
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        rc = select(sock + 1, &rfds, NULL, NULL, &tv);
        if (rc < 0) {
            if (!(server->flags & FTPD_ACTIVE))
                break;
            ftpd_log(LOG_ERROR, "%s: select() failed, errno=%d", __func__,
                     errno);
            continue;
        }
        if (rc == 0)
            continue;

        if (!FD_ISSET(sock, &rfds))
            continue;

        /* Accept new connection */
        len = sizeof(caddr);
        rc = accept(sock, (struct sockaddr *)&caddr, &len);
        if (rc < 0) {
            ftpd_log(LOG_WARN, "%s: accept() failed, errno=%d", __func__,
                     errno);
            continue;
        }

        /* Check session limit */
        if (server->num_sessions >= server->config.max_sessions) {
            ftpd_log(LOG_WARN, "%s: max sessions reached, rejecting",
                     __func__);
            closesocket(rc);
            continue;
        }

        /* Create session and queue to worker pool */
        sess = ftpd_session_new(server, rc);
        if (!sess) {
            closesocket(rc);
            continue;
        }

        ftpd_log(LOG_INFO, "%s: connection from %u.%u.%u.%u:%d", __func__,
                 (ntohl(caddr.sin_addr.s_addr) >> 24) & 0xFF,
                 (ntohl(caddr.sin_addr.s_addr) >> 16) & 0xFF,
                 (ntohl(caddr.sin_addr.s_addr) >> 8) & 0xFF,
                 ntohl(caddr.sin_addr.s_addr) & 0xFF,
                 ntohs(caddr.sin_port));

        cthread_queue_add(server->mgr, sess);
    }

    closesocket(sock);
    server->listen_sock = -1;

    return 0;
}

/* ====================================================================
** Shutdown -- close listener, terminate threads, cleanup
**
** Resource cleanup follows UFSD order:
**   1. Close listener socket
**   2. Terminate thread manager (waits for workers)
**   3. Free trace buffer
** ================================================================= */
static void
terminate(ftpd_server_t *server)
{
    ftpd_log(LOG_INFO, "%s: shutting down...", __func__);

    server->flags &= ~FTPD_ACTIVE;
    server->flags |= FTPD_QUIESCE;

    /* Close listener socket to unblock accept */
    if (server->listen_sock >= 0) {
        closesocket(server->listen_sock);
        server->listen_sock = -1;
        ftpd_log(LOG_INFO, "%s: listener closed", __func__);
    }

    /* Terminate thread manager (waits for workers to finish) */
    if (server->mgr) {
        cthread_manager_term(&server->mgr);
        server->mgr = NULL;
        ftpd_log(LOG_INFO, "%s: thread manager terminated", __func__);
    }

    /* Free trace buffer */
    ftpd_trace_free();
    ftpd_log(LOG_INFO, "%s: trace buffer freed", __func__);
}
