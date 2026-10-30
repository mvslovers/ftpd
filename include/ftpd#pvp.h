#ifndef FTPD_PVP_H
#define FTPD_PVP_H
/*
** FTPD Passive Listener Port Walk
**
** PASV and EPSV need a listening socket on a free port of the PASVPORTS
** range.  The stack can hand the same port to two sessions that ask at the
** same moment: both bind() calls succeed and the second listen() fails with
** EADDRINUSE (#315).  That is the same "port taken" as a failed bind(), so
** the walk goes on with the next port and only gives up when the range is
** used up.
**
** The socket calls are passed in, so the walk is free of project and MVS
** headers and a host test can drive it with a stand-in that fails where the
** stack does (test/tstpvp.c).
*/

#define FTPD_PVP_FULL   (-1)    /* every port of the range is taken        */
#define FTPD_PVP_ERROR  (-2)    /* socket() or listen() failed otherwise   */

typedef struct ftpd_pvp_ops {
    int  (*sock)(void *ctx);                    /* new socket, < 0 on failure */
    int  (*bind)(void *ctx, int sock, int port);/* 0 = bound                  */
    int  (*listen)(void *ctx, int sock);        /* 0 = listening, else errno  */
    void (*close)(void *ctx, int sock);
} ftpd_pvp_ops_t;

/*
** Open a listening socket on the first free port of lo..hi.
**
** Returns the socket and stores the port in *port, FTPD_PVP_FULL when the
** whole range is taken, FTPD_PVP_ERROR on any other failure.  On a negative
** return no socket is left open.
**
** A port is taken when bind() fails or when listen() answers EADDRINUSE.
** Any other listen() error ends the walk at once: the next port would fail
** the same way.  A socket that failed listen() is closed and replaced, since
** it is bound already.
*/
int ftpd_pvp_open(const ftpd_pvp_ops_t *ops, void *ctx,
                  int lo, int hi, int *port);

#endif /* FTPD_PVP_H */
