#ifndef FTPD_H
#define FTPD_H
/*
** FTPD - Standalone FTP Server for MVS 3.8j
**
** Main header: system includes, libc370 includes, shared constants,
** core types, and server state.  Per-module headers in ftpd#xxx.h.
*/
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>

#include <mvs/crt.h>                /* C runtime area               */
#include <mvs/wto.h>                /* write to operator            */
#include <mvs/thread.h>             /* threads, thread management   */
#include <mvs/console.h>            /* console information blocks   */
#include <mvs/racf.h>               /* security environment         */
#include <mvs/recovery.h>           /* try(), tryrc() ESTAE recovery */

#include "ftpd#adr.h"               /* address parameter parsing    */
#include "ftpd#cfg.h"               /* configuration                */
#include "ftpd#log.h"               /* logging & trace              */
#include "ftpd#rep.h"               /* FTP reply codes              */
#include "ftpd#xlt.h"               /* EBCDIC/ASCII translation     */

typedef unsigned char   UCHAR;

/* --- Product name ---
**
** The version is NOT here: it comes from MBT_VERSION in the generated
** <buildstamp.h>, so it cannot drift from VERSION the way a second copy
** would.  buildstamp.h stays out of this header on purpose -- -MMD makes
** it a prerequisite of every object that includes it, and a new commit
** would then rebuild the whole project instead of the few translation
** units that actually name the version (mbt#59).
*/
#define FTPD_PRODUCT        "MVS 3.8j FTPD Server"

/* --- Filesystem / filetype modes --- */
#define FT_SEQ              0       /* sequential dataset mode       */
#define FT_JES              1       /* JES interface mode            */

#define FS_MVS              0       /* MVS dataset mode              */
#define FS_UFS              1       /* UFS filesystem mode           */

/* --- Session states --- */
#define SESS_GREETING       0       /* sending 220 banner            */
#define SESS_AUTH_USER      1       /* waiting for USER              */
#define SESS_AUTH_PASS      2       /* waiting for PASS              */
#define SESS_READY          3       /* authenticated, ready          */
#define SESS_COMMAND        4       /* processing command            */
#define SESS_TRANSFER       5       /* data transfer in progress     */
#define SESS_CLOSING        6       /* session closing               */

/* --- Transfer types --- */
#define XFER_TYPE_A         'A'     /* ASCII                         */
#define XFER_TYPE_E         'E'     /* EBCDIC                        */
#define XFER_TYPE_I         'I'     /* Image (binary)                */

/* --- File structure --- */
#define XFER_STRU_F         'F'     /* File structure                */
#define XFER_STRU_R         'R'     /* Record structure              */

/* --- Data connection mode --- */
#define DATA_NONE           0       /* no data connection setup       */
#define DATA_PORT           1       /* active mode (PORT)            */
#define DATA_PASV           2       /* passive mode (PASV)           */

/* --- Limits --- */
#define FTPD_MAX_CMD_LEN    512     /* max FTP command line length   */
#define FTPD_MAX_DSN_LEN    44      /* max MVS dataset name length   */
#define FTPD_MAX_MBR_LEN    8       /* max PDS member name length    */
#define FTPD_MAX_PATH_LEN   256     /* max UFS path length           */
#define FTPD_MAX_USER_LEN   8       /* max userid length             */
#define FTPD_DATA_BUF_SIZE  4096    /* data transfer buffer size     */
#define FTPD_LISTEN_BACKLOG 128     /* completed connections the host
                                    ** holds for accept(); see
                                    ** socket_thread() (#327)          */

/* --- ABEND recovery --- */
#define FTPD_MAX_RECOVER    3       /* consecutive per-command ABEND
                                    ** recoveries before the session is
                                    ** closed (guards against a wedged
                                    ** session recovering forever)     */

/* --- Allocation defaults for new datasets --- */
typedef struct ftpd_alloc {
    char            recfm[4];       /* RECFM for new datasets        */
    int             lrecl;          /* LRECL                          */
    int             blksize;        /* BLKSIZE                        */
    int             primary;        /* primary allocation             */
    int             secondary;      /* secondary allocation           */
    char            spacetype[4];   /* TRK or CYL                    */
    char            volume[7];      /* target volume                  */
    char            unit[5];        /* device type                    */
    int             dirblks;        /* PDS directory blocks           */
    int             retpd;          /* retention in days, 0 = none    */
} ftpd_alloc_t;

/* --- Forward declarations --- */
typedef struct ftpd_session ftpd_session_t;
typedef struct ftpd_server  ftpd_server_t;

/* Major name of the single-instance ENQ (SCOPE=SYSTEM).  The minor name
** carries the listen port, so one FTPD per port is allowed and a second
** start on a port already served is refused. */
#define FTPD_ENQ_QNAME      "FTPD"

/* --- Global server state --- */
struct ftpd_server {
    char            eye[8];         /* eye catcher                   */
#define FTPD_EYE    "*FTPD*"

    /* Server flags */
    /* volatile: written by one TCB and polled by another.  The socket
    ** thread's bind retry watches flags for a /P, main's event loop
    ** watches it for shutdown, and main watches listen_sock for the
    ** listener coming up.  Without it cc370 -O1 hoists the load out of
    ** the polling loop and the poller never sees the change (#113). */
    volatile unsigned flags;
#define FTPD_ACTIVE         0x01    /* server is running             */
#define FTPD_QUIESCE        0x02    /* shutdown in progress          */

    ftpd_config_t   config;         /* server configuration          */
    ftpd_logst_t    log;            /* log level + trace ring state;
                                    ** here rather than file scope in
                                    ** ftpd#log.c because module storage
                                    ** is key 0 under APF (#101).  Read
                                    ** through grtapp2, not this field  */
    char            enq_rname[16];  /* "FTPD.PORT.nnnnn": the single
                                    ** instance ENQ, held by main's TCB
                                    ** for the life of the server      */
    int             authorized;     /* the task is APF-authorized, set
                                    ** once by main() after the APF setup.
                                    ** 0 (also the memset value) is the
                                    ** fail-closed reading: racf_login()
                                    ** and racf_set_acee() MODESET into
                                    ** supervisor state and ABEND S047
                                    ** without it, so neither is called
                                    ** (#258)                          */
    int             unauth_logged;  /* the unauthorized PASS refusal has
                                    ** been logged, once per server     */
    ACEE            *stc_acee;      /* identity RAKF gave the STC, read at
                                    ** init; recovery resets ASXBSENV
                                    ** to this after an ABEND          */
    int             acee_lock;      /* never read — its ADDRESS is the
                                    ** ENQ resource serializing the
                                    ** identity windows (ftpd#aut.c).
                                    ** Lives in the server struct so it
                                    ** is one anchor per address space  */
    volatile int    listen_sock;    /* listening socket fd; see flags */
    CTHDMGR         *mgr;          /* thread manager                */
    CTHDTASK        *sock_task;    /* socket listener thread         */
    unsigned        wakeup_ecb;    /* posted to break main WAIT      */
    int             num_sessions;   /* active session count           */
    long            total_sessions; /* total sessions since start     */
    long            total_bytes_in; /* total bytes received           */
    long            total_bytes_out;/* total bytes sent               */
    unsigned        total_recover;  /* ABEND recoveries since start
                                    ** (STC-lifetime; observability for
                                    ** documented residual leaks)      */
};

/*
** Process a Console Information Block (operator command).
** Called from the main event loop for each CIB.
*/
int ftpd_process_cib(ftpd_server_t *server, CIB *cib)      asm("FTPCONPC");

#endif /* FTPD_H */
