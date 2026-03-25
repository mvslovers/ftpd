#ifndef FTPD_H
#define FTPD_H
/*
** FTPD - Standalone FTP Server for MVS 3.8j
**
** Main header: system includes, crent370 includes, shared constants,
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

#include "clibcrt.h"                /* C runtime area               */
#include "clibwto.h"                /* write to operator            */
#include "clibthrd.h"               /* basic threads                */
#include "clibthdi.h"               /* thread management            */
#include "clibcib.h"                /* console information blocks   */
#include "socket.h"                 /* sockets via DYN75            */
#include "racf.h"                   /* security environment         */

#include "ftpd#cfg.h"               /* configuration                */
#include "ftpd#log.h"               /* logging & trace              */

typedef unsigned char   UCHAR;

/* --- Version --- */
#define FTPD_VERSION        "1.0.0"
#define FTPD_VERSION_STR    "MVS 3.8j FTPD Server " FTPD_VERSION

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

/* --- FTP Reply Codes --- */
#define FTP_125             125     /* data conn open, xfer starting */
#define FTP_150             150     /* file ok, opening data conn    */
#define FTP_200             200     /* command okay                  */
#define FTP_211             211     /* system status / FEAT          */
#define FTP_213             213     /* file status (SIZE)            */
#define FTP_214             214     /* help message                  */
#define FTP_215             215     /* system type                   */
#define FTP_220             220     /* service ready                 */
#define FTP_221             221     /* closing control connection    */
#define FTP_226             226     /* closing data, xfer complete   */
#define FTP_227             227     /* entering passive mode         */
#define FTP_230             230     /* user logged in                */
#define FTP_250             250     /* file action okay              */
#define FTP_257             257     /* pathname created              */
#define FTP_331             331     /* username ok, need password    */
#define FTP_350             350     /* pending further info          */
#define FTP_421             421     /* service not available         */
#define FTP_425             425     /* can't open data connection    */
#define FTP_426             426     /* connection closed, xfer abort */
#define FTP_450             450     /* file unavailable (busy)       */
#define FTP_451             451     /* local error                   */
#define FTP_452             452     /* insufficient storage          */
#define FTP_500             500     /* unrecognized command          */
#define FTP_501             501     /* syntax error in parameters    */
#define FTP_502             502     /* command not implemented       */
#define FTP_504             504     /* not impl for that parameter   */
#define FTP_530             530     /* not logged in                 */
#define FTP_550             550     /* file unavailable              */
#define FTP_552             552     /* exceeded storage allocation   */
#define FTP_553             553     /* file name not allowed         */

/* --- Limits --- */
#define FTPD_MAX_CMD_LEN    512     /* max FTP command line length   */
#define FTPD_MAX_DSN_LEN    44      /* max MVS dataset name length   */
#define FTPD_MAX_MBR_LEN    8       /* max PDS member name length    */
#define FTPD_MAX_PATH_LEN   256     /* max UFS path length           */
#define FTPD_MAX_USER_LEN   8       /* max userid length             */
#define FTPD_DATA_BUF_SIZE  4096    /* data transfer buffer size     */

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
} ftpd_alloc_t;

/* --- Forward declarations --- */
typedef struct ftpd_session ftpd_session_t;
typedef struct ftpd_server  ftpd_server_t;

/* --- Global server state --- */
struct ftpd_server {
    char            eye[8];         /* eye catcher                   */
#define FTPD_EYE    "*FTPD*"

    /* Server flags */
    unsigned        flags;
#define FTPD_ACTIVE         0x01    /* server is running             */
#define FTPD_QUIESCE        0x02    /* shutdown in progress          */

    ftpd_config_t   config;         /* server configuration          */
    int             listen_sock;    /* listening socket fd            */
    CTHDMGR         *mgr;          /* thread manager                */
    unsigned        wakeup_ecb;     /* ECB posted by socket thread   */
    int             num_sessions;   /* active session count           */
    long            total_sessions; /* total sessions since start     */
    long            total_bytes_in; /* total bytes received           */
    long            total_bytes_out;/* total bytes sent               */
};

extern ftpd_server_t *ftpd_server;

/*
** Process a Console Information Block (operator command).
** Called from the main event loop for each CIB.
*/
int ftpd_process_cib(ftpd_server_t *server, CIB *cib)      asm("FTPCONPC");

#endif /* FTPD_H */
