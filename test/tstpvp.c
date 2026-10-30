/* TSTPVP.C
** Tests for ftpd_pvp_open() -- the passive listener port walk (issue #315).
**
** With sessions issuing PASV at the same moment, the stack lets two of them
** bind() the same port and fails the second listen() with EADDRINUSE.  FTPD
** answered 425 although the rest of the PASVPORTS range was free.  The stand-in
** below fails where the stack does; the walk must go on to the next port and
** answer FULL only when the range is used up.  A listen() failure other than
** EADDRINUSE ends the walk at once.
**
** ftpd#pvp.c is free of project and MVS headers, so this is a DUAL test.
*/
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "ftpd#pvp.h"
#include <mbtcheck.h>

#define MAXPORT 16

typedef struct fake {
    int bind_fail[MAXPORT];     /* port - 100 -> bind() fails            */
    int listen_err[MAXPORT];    /* port - 100 -> listen() errno, 0 = ok  */
    int next_sock;
    int open_socks;
    int sock_fail_after;        /* socket() fails from this call on, 0 = never */
    int sock_calls;
    int binds;
    int listens;
    int bound_port[8];          /* per socket: port it is bound to, 0 = none */
} fake_t;

static int
f_sock(void *ctx)
{
    fake_t *f = ctx;

    f->sock_calls++;
    if (f->sock_fail_after && f->sock_calls >= f->sock_fail_after)
        return -1;
    f->open_socks++;
    f->bound_port[f->next_sock] = 0;
    return f->next_sock++;
}

static int
f_bind(void *ctx, int sock, int port)
{
    fake_t *f = ctx;

    f->binds++;
    if (f->bound_port[sock] != 0)
        return -1;              /* a bound socket cannot bind again */
    if (f->bind_fail[port - 100])
        return -1;
    f->bound_port[sock] = port;
    return 0;
}

static int
f_listen(void *ctx, int sock)
{
    fake_t *f = ctx;

    f->listens++;
    return f->listen_err[f->bound_port[sock] - 100];
}

static void
f_close(void *ctx, int sock)
{
    fake_t *f = ctx;

    (void)sock;
    f->open_socks--;
}

static const ftpd_pvp_ops_t ops = { f_sock, f_bind, f_listen, f_close };

int
main(void)
{
    fake_t f;
    int port;
    int rc;

    printf("=== FTPD passive port walk tests (#315) ===\n");

    memset(&f, 0, sizeof(f));
    rc = ftpd_pvp_open(&ops, &f, 100, 104, &port);
    CHECK(rc >= 0 && port == 100, "free range: first port, socket returned");
    CHECK(f.open_socks == 1, "free range: one socket open");

    memset(&f, 0, sizeof(f));
    f.listen_err[0] = EADDRINUSE;
    rc = ftpd_pvp_open(&ops, &f, 100, 104, &port);
    CHECK(rc >= 0 && port == 101,
          "listen() EADDRINUSE on the first port: next port taken (was 425)");
    CHECK(f.open_socks == 1, "EADDRINUSE: the spoiled socket was closed");

    memset(&f, 0, sizeof(f));
    f.bind_fail[0] = 1;
    f.listen_err[1] = EADDRINUSE;
    f.listen_err[2] = EADDRINUSE;
    rc = ftpd_pvp_open(&ops, &f, 100, 104, &port);
    CHECK(rc >= 0 && port == 103,
          "bind() and listen() refusals mixed: first usable port wins");
    CHECK(f.open_socks == 1, "mixed refusals: one socket open");

    memset(&f, 0, sizeof(f));
    f.listen_err[0] = EADDRINUSE;
    f.listen_err[1] = EADDRINUSE;
    f.listen_err[2] = EADDRINUSE;
    rc = ftpd_pvp_open(&ops, &f, 100, 102, &port);
    CHECK(rc == FTPD_PVP_FULL,
          "every port refused at listen(): range used up -> FULL");
    CHECK(f.open_socks == 0, "range used up: no socket left open");

    memset(&f, 0, sizeof(f));
    f.bind_fail[0] = f.bind_fail[1] = 1;
    rc = ftpd_pvp_open(&ops, &f, 100, 101, &port);
    CHECK(rc == FTPD_PVP_FULL, "every port refused at bind(): FULL");
    CHECK(f.open_socks == 0, "bind() refusals: no socket left open");

    memset(&f, 0, sizeof(f));
    f.listen_err[0] = ECONNRESET;
    rc = ftpd_pvp_open(&ops, &f, 100, 104, &port);
    CHECK(rc == FTPD_PVP_ERROR, "another listen() error ends the walk: ERROR");
    CHECK(f.listens == 1, "another listen() error: no second attempt");
    CHECK(f.open_socks == 0, "another listen() error: no socket left open");

    memset(&f, 0, sizeof(f));
    f.sock_fail_after = 1;
    rc = ftpd_pvp_open(&ops, &f, 100, 104, &port);
    CHECK(rc == FTPD_PVP_ERROR, "socket() fails at once: ERROR");

    memset(&f, 0, sizeof(f));
    f.listen_err[0] = EADDRINUSE;
    f.sock_fail_after = 2;
    rc = ftpd_pvp_open(&ops, &f, 100, 104, &port);
    CHECK(rc == FTPD_PVP_ERROR,
          "replacement socket() fails after EADDRINUSE: ERROR");
    CHECK(f.open_socks == 0, "replacement socket() fails: nothing left open");

    memset(&f, 0, sizeof(f));
    f.listen_err[0] = EADDRINUSE;
    rc = ftpd_pvp_open(&ops, &f, 100, 100, &port);
    CHECK(rc == FTPD_PVP_FULL, "range of one port, taken at listen(): FULL");
    CHECK(f.open_socks == 0, "range of one: no socket left open");

    return mbt_test_summary("TSTPVP");
}
