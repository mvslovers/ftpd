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
    server.flags |= FTPD_ACTIVE;
    ftpd_server = &server;

    /* Get COM area and set CIB limit BEFORE creating any threads.
    **
    ** __gtcom() and __cibset() acquire the CRT PPA lock internally.
    ** Thread startup (@@CRTSET) takes an EXCLUSIVE lock on the same PPA.
    ** If a thread is running when main calls __cibset, the shared lock
    ** request collides with the exclusive holder and the PPA chain is
    ** corrupted.  Performing all CRT-dependent console setup while main
    ** is the only TCB eliminates the race (UFSD pattern).
    */
    com = __gtcom();
    if (!com) {
        ftpd_log_wto("FTPD099E COM area not available");
        return 8;
    }
    __cibset(5);

    /* Initialize server (config, trace, socket thread, worker pool) */
    rc = initialize(&server, argc, argv);
    if (rc != 0) {
        ftpd_log_wto("FTPD099E Initialization failed, rc=%d", rc);
        return rc;
    }

    ftpd_log_wto("FTPD001I FTPD %s started on port %d",
                 FTPD_VERSION, server.config.port);

    /* Kick the thread manager so workers start accepting sessions.
    ** This must happen AFTER __cibset and FTPD_ACTIVE are set.
    */
    if (server.mgr) {
        cthread_post(&server.mgr->wait, CTHDMGR_POST_DATA);
    }

    /* ----------------------------------------------------------------
    ** Main event loop
    **
    ** 1. Drain ALL pending CIBs unconditionally
    ** 2. Check shutdown flag before sleep
    ** 3. STIMER WAIT 1 second, then loop
    **
    ** CIBs are drained unconditionally because MVS may queue a
    ** CIBSTART at startup without posting the ECB.
    **
    ** We use STIMER polling (1 second) instead of WAIT ECBLIST
    ** because the console ECB (com->comecbpt) is in key-0 storage
    ** and the wakeup ECB (stack) is in key-8 storage.  Mixing
    ** storage keys in a single ECBLIST causes S0A3 on MVS 3.8j
    ** (OS/VS2 SPLS GC28-0683 §WAIT).  UFSD avoids this because
    ** both its ECBs (console + server_ecb) are in key-0 (CSA).
    ** 1-second polling gives acceptable latency for console
    ** commands and shutdown responsiveness.
    ** ---------------------------------------------------------------- */
    while (server.flags & FTPD_ACTIVE) {

        /* Drain all pending CIBs unconditionally */
        while ((cib = __cibget()) != NULL) {
            ftpd_process_cib(&server, cib);
            __cibdel(cib);
            if (!(server.flags & FTPD_ACTIVE)) break;
        }

        if (!(server.flags & FTPD_ACTIVE)) break;

        /* Sleep 1 second (100 centiseconds), then check again */
        __asm__("STIMER WAIT,BINTVL==F'100'");
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
    server->sock_task = cthread_create_ex(socket_thread, server, NULL,
                                          32 * 1024);

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

    /* Create listening socket.
    ** On failure, log the error via WTO and return — do NOT call
    ** signal_shutdown.  The main event loop stays running so the
    ** operator can see the error and /P the server.
    */
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        ftpd_log_wto("FTPD050E socket() failed, errno=%d", errno);
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
        ftpd_log_wto("FTPD051E bind() failed on port %d, errno=%d",
                     server->config.port, errno);
        closesocket(sock);
        return 8;
    }

    if (listen(sock, 10) < 0) {
        ftpd_log_wto("FTPD052E listen() failed, errno=%d", errno);
        closesocket(sock);
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
** Shutdown -- terminate threads, close listener, cleanup
**
** Shutdown sequence (modelled after HTTPD terminate):
**   1. Set QUIESCE flag (FTPD_ACTIVE already cleared by cmd_shutdown)
**   2. Wait for socket thread to notice FTPD_ACTIVE==0 and exit
**   3. Close listener socket (fallback if socket thread didn't)
**   4. Terminate thread manager (posts workers for shutdown)
**   5. Free trace buffer
**
** IMPORTANT: Do NOT closesocket the listener before the socket
** thread exits.  On MVS 3.8j closing a socket while another TCB
** has it in select() leaves the TCP/IP control block in an
** undefined state and can hang select() indefinitely.  The socket
** thread checks FTPD_ACTIVE on every select() timeout (1 s) and
** closes its own socket on exit.
** ================================================================= */
static void
terminate(ftpd_server_t *server)
{
    ftpd_log_wto("FTPD095I terminate: starting shutdown sequence");

    server->flags &= ~FTPD_ACTIVE;
    server->flags |= FTPD_QUIESCE;

    /* Wait for socket thread to exit.
    ** The socket thread checks FTPD_ACTIVE on every select() timeout
    ** (1 second) and exits when the flag is cleared.
    ** Use cthread_delete (like HTTPD) — it handles DETACH internally.
    ** Do NOT call cthread_detach separately: MVS DETACH blocks until
    ** the subtask ends, and if the thread is still in select() the
    ** main task hangs here.
    */
    if (server->sock_task) {
        int i;
        for (i = 0; i < 50; i++) {
            if (server->sock_task->termecb & 0x40000000U)
                break;
            __asm__("STIMER WAIT,BINTVL==F'10'");
        }
        if (!(server->sock_task->termecb & 0x40000000U)) {
            ftpd_log_wto("FTPD095W socket thread did not terminate "
                         "in 5 seconds, force cleanup");
        }
        cthread_delete(&server->sock_task);
        server->sock_task = NULL;
        ftpd_log_wto("FTPD095I terminate: socket thread cleaned up");
    }

    /* Close listener socket (fallback — socket thread normally closes
    ** its own copy, but guard against the case where it didn't). */
    if (server->listen_sock >= 0) {
        closesocket(server->listen_sock);
        server->listen_sock = -1;
    }

    /* Terminate thread manager (posts workers for shutdown) */
    ftpd_log_wto("FTPD095I terminate: stopping thread manager");
    if (server->mgr) {
        cthread_manager_term(&server->mgr);
        server->mgr = NULL;
        ftpd_log_wto("FTPD095I terminate: thread manager stopped");
    }

    /* Free trace buffer */
    ftpd_trace_free();
}
