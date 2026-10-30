/*
** FTPD Passive Listener Port Walk -- see ftpd#pvp.h
*/
#include <errno.h>
#include <stddef.h>

#include "ftpd#pvp.h"

int
ftpd_pvp_open(const ftpd_pvp_ops_t *ops, void *ctx,
              int lo, int hi, int *port)
{
    int sock;
    int p;
    int err;

    sock = ops->sock(ctx);
    if (sock < 0)
        return FTPD_PVP_ERROR;

    for (p = lo; p <= hi; p++) {
        if (ops->bind(ctx, sock, p) != 0)
            continue;

        err = ops->listen(ctx, sock);
        if (err == 0) {
            *port = p;
            return sock;
        }

        ops->close(ctx, sock);
        if (err != EADDRINUSE)
            return FTPD_PVP_ERROR;

        /* bound already, so it cannot try another port */
        sock = ops->sock(ctx);
        if (sock < 0)
            return FTPD_PVP_ERROR;
    }

    ops->close(ctx, sock);
    return FTPD_PVP_FULL;
}
