/*
** TSTQUE -- ftpd#177: a connection the worker pool refuses must not leak.
**
** The accept loop used to ignore cthread_queue_add()'s return.  When the
** manager is quiescing or stopped (libc370 2.19.0 and later answer -1 there)
** or has no storage for the queue element, nobody owned the new session: its
** control socket stayed open, the client got no banner and was never
** disconnected, and the session storage was lost.
** ftpd_session_queue() now owns that case: 421, close, free, answer -1.
**
** Two rounds over a real loopback connection, each with an accepted socket
** wrapped in a real session, handed to a real thread manager:
**
**   round 1  manager RUNNING: ftpd_session_queue() answers 0 and the worker
**            receives the session.  Without it round 2 proves nothing -- a
**            queue that refused everything would pass.
**   round 2  manager QUIESCE (the state cthread_manager_term() puts it in
**            while FTPD shuts down): ftpd_session_queue() answers -1, the
**            client reads "421" and then end-of-file: the server end of the
**            connection is closed, nothing is left hanging.
**
** The end-of-file probe is calibrated on a socket of its own first: a client
** must see end-of-file exactly when the server end is closed, otherwise
** "the client saw end-of-file" would say nothing.  (closesocket() on an
** already closed descriptor answers 0 in libc370, so it cannot be the probe.)
**
** What it cannot show is the free() of the session storage: libc370 has no
** heap counter to read.  ftpd_session_free() is the same function the worker
** uses at the end of every session, and its socket close is what is observed.
**
** The manager state is set directly: the struct is public in <mvs/thread.h>
** and the test must not depend on winning a race against a real shutdown.
** The manager task writes mgr->state every time it wakes (WAITING before its
** wait, RUNNING after it) and does not look at QUIESCE, so the store and the
** ftpd_session_queue() call in round 2 are made under lock(mgr, 0), the lock
** the manager takes around each of those writes.  Without it, the wake the
** worker causes when it returns to cthread_worker_wait() after round 1 can
** land in between and reset the state (ftpd#411).
**
** MVS-only: thread manager, SVC 75 sockets.
** PARM='<port>' is the loopback listen port (default 21771).
**
** RC 0 = all checks passed.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <mvs/socket.h>
#include <mvs/thread.h>
#include <mvs/ecb.h>
#include <mvs/lock.h>
#include <mbtcheck.h>

#include "ftpd.h"
#include "ftpd#log.h"
#include "ftpd#ses.h"
#include "tstloop.h"

#define DFLT_PORT       21771

/* What the worker saw.  Heap, written by the worker task, read by main. */
struct ctx {
    volatile int    got;        /* sessions received                  */
    volatile int    sock;       /* control socket of the last one     */
};

/* Stand-in for ftpd_session_run(): take sessions, free them, count them. */
static int
worker(void *udata, CTHDWORK *work)
{
    struct ctx *ctx = (struct ctx *)udata;
    char       *data = NULL;
    int         rc;

    for (;;) {
        rc = cthread_worker_wait(work, &data);
        if (rc == CTHDWORK_POST_SHUTDOWN)
            break;
        if (rc != CTHDWORK_POST_REQUEST || !data)
            continue;

        ctx->sock = ((ftpd_session_t *)data)->ctrl_sock;
        ctx->got++;
        ftpd_session_free((ftpd_session_t *)data);
        data = NULL;
    }
    return 0;
}

/* Read from the client until end-of-file.  Returns the byte count, -1 when
** nothing ended it within the time allowed -- the "hanging connection". */
static int
drain(int fd, char *buf, int max)
{
    int n = 0;

    for (;;) {
        fd_set         rfds;
        struct timeval tv;
        int            rc;

        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        tv.tv_sec = 5;
        tv.tv_usec = 0;
        rc = select(fd + 1, &rfds, NULL, NULL, &tv);
        if (rc <= 0)
            return -1;

        rc = recv(fd, buf + n, max - n, 0);
        if (rc < 0)
            return -1;
        if (rc == 0)
            return n;
        n += rc;
        if (n >= max)
            return n;
    }
}

int
main(int argc, char **argv)
{
    unsigned short     port = DFLT_PORT;
    ftpd_server_t     *server;
    struct ctx         ctx;
    CTHDMGR           *mgr;
    ftpd_session_t    *sess;
    int                lsn, cli, srv;
    int                rc, n, i, locked;
    char               buf[128];

    if (argc > 1 && argv[1] && argv[1][0])
        port = (unsigned short)atoi(argv[1]);

    printf("TSTQUE -- ftpd#177: a refused connection must be answered "
           "and released (port %d)\n\n", port);

    server = calloc(1, sizeof(*server));
    if (!server) {
        printf("FAIL: no storage for the server block\n");
        return 8;
    }
    memset(&ctx, 0, sizeof(ctx));

    lsn = loop_listen(port);
    if (lsn < 0) {
        printf("FAIL: cannot listen on the loopback port, errno=%d\n", errno);
        return 8;
    }

    /* ---- calibrate the probe ---------------------------------------- */

    if (connected_pair(lsn, port, &cli, &srv) != 0) {
        printf("FAIL: loopback connect/accept\n");
        return 8;
    }
    CHECK(closesocket(srv) == 0, "probe: closing an open socket answers 0");
    CHECK(drain(cli, buf, sizeof(buf)) == 0,
          "probe: the client sees end-of-file after the server end closes");
    closesocket(cli);

    mgr = cthread_manager_init(1, worker, &ctx, 64 * 1024);
    if (!mgr) {
        printf("FAIL: cthread_manager_init\n");
        return 8;
    }
    server->mgr = mgr;
    /* FTPD leaves mintask at 0; with libc370 2.24.1 a pool like that never
    ** started a worker for a queued element in this test.  One worker is
    ** started here, as HTTPD does, so round 1 has someone to receive. */
    mgr->mintask = 1;
    cthread_post(&mgr->wait, CTHDMGR_POST_DATA);

    /* ---- round 1: a healthy pool takes the session ------------------ */

    if (connected_pair(lsn, port, &cli, &srv) == 0) {
        sess = ftpd_session_new(server, srv);
        CHECK(sess != NULL, "round 1: session allocated");
        if (sess) {
            rc = ftpd_session_queue(server, sess);
            CHECK_EQ(rc, 0, "round 1: a running pool queues the session");

            for (i = 0; i < 50 && ctx.got == 0; i++)
                nap(1);
            CHECK_EQ(ctx.got, 1, "round 1: the worker received the session");
            CHECK_EQ(ctx.sock, srv, "round 1: ... with its control socket");

            n = drain(cli, buf, sizeof(buf));
            CHECK(n == 0, "round 1: the worker released it -- no 421 for a "
                          "session that was queued");
        }
        closesocket(cli);
    } else {
        CHECK(0, "round 1: loopback connect/accept");
    }

    /* ---- round 2: a quiescing pool refuses it ----------------------- */

    if (connected_pair(lsn, port, &cli, &srv) == 0) {
        sess = ftpd_session_new(server, srv);
        CHECK(sess != NULL, "round 2: session allocated");
        if (sess) {
            /* cthread_queue_add() sees the lock held and leaves it alone */
            locked = lock(mgr, 0);
            mgr->state = CTHDMGR_STATE_QUIESCE;
            rc = ftpd_session_queue(server, sess);
            if (locked == 0)
                unlock(mgr, 0);
            CHECK_EQ(rc, -1, "round 2: a quiescing pool refuses the session");
            CHECK_EQ(ctx.got, 1, "round 2: the worker received nothing");

            n = drain(cli, buf, sizeof(buf));
            CHECK(n >= 3, "round 2: the client was answered, and then "
                          "disconnected (no hanging connection)");
            if (n >= 3)
                CHECK(buf[0] == 0x34 && buf[1] == 0x32 && buf[2] == 0x31,
                      "round 2: the answer is 421 (ASCII on the wire)");
        }
        closesocket(cli);
    } else {
        CHECK(0, "round 2: loopback connect/accept");
    }

    /* ---- the server keeps going: the next connection is served ------ */

    mgr->state = CTHDMGR_STATE_RUNNING;

    if (connected_pair(lsn, port, &cli, &srv) == 0) {
        sess = ftpd_session_new(server, srv);
        rc = sess ? ftpd_session_queue(server, sess) : -2;
        CHECK_EQ(rc, 0, "after the refusal: a healthy pool queues again");
        for (i = 0; i < 50 && ctx.got < 2; i++)
            nap(1);
        CHECK_EQ(ctx.got, 2, "after the refusal: the worker got the next one");
        closesocket(cli);
    } else {
        CHECK(0, "after the refusal: loopback connect/accept");
    }

    cthread_manager_term(&mgr);
    closesocket(lsn);
    free(server);

    return mbt_test_summary("TSTQUE");
}
