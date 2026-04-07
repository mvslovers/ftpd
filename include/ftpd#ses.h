#ifndef FTPD_SES_H
#define FTPD_SES_H
/*
** FTPD Session Management
*/
#include "ftpd#xlt.h"

/* Forward declaration — full definition in libufs.h */
struct libufs_ufs;

/* --- Session structure --- */
struct ftpd_session {
    char            eye[8];         /* eye catcher                   */
#define FTPD_SES_EYE "*FTSES*"

    /* Sockets */
    int             ctrl_sock;      /* control connection socket     */
    int             data_sock;      /* data connection socket        */
    int             pasv_sock;      /* passive listen socket         */

    /* Data connection target (PORT mode) */
    unsigned        data_addr;      /* client IP for PORT            */
    int             data_port;      /* client port for PORT          */
    int             data_mode;      /* DATA_NONE, DATA_PORT, DATA_PASV */

    /* State */
    int             state;          /* session state                 */
    int             filetype;       /* FT_SEQ or FT_JES             */
    int             fsmode;         /* FS_MVS or FS_UFS             */
    int             prev_fsmode;    /* saved fsmode during JES mode  */
    char            type;           /* 'A','E','I' transfer type     */
    char            stru;           /* 'F','R' structure             */

    /* Authentication */
    char            user[9];        /* userid (8 chars + null)       */
    char            pass[9];        /* password (for JES USER= inj.) */
    int             authenticated;  /* login complete flag            */
    int             auth_attempts;  /* failed login attempts          */
    ACEE            *acee;          /* RACF ACEE pointer             */

    /* MVS context */
    char            hlq[45];        /* high-level qualifier/prefix   */
    char            mvs_cwd[45];    /* current MVS "directory"       */
    int             in_pds;         /* 1 = CWD is inside a PDS       */
    char            pds_name[45];   /* PDS dataset name when in_pds  */

    /* UFS context */
    struct libufs_ufs *ufs;         /* UFSD session handle (lazy)    */
    char            ufs_cwd[256];   /* current UFS path              */

    /* JES context */
    int             jes_level;      /* JES interface level (1/2)     */
    char            jes_owner[9];   /* JESOWNER filter               */
    char            jes_jobname[9]; /* JESJOBNAME filter             */
    char            jes_status[8];  /* JESSTATUS filter              */

    /* SITE allocation defaults */
    ftpd_alloc_t    alloc;

    /* EPSV ALL lock (RFC 2428) */
    int             epsv_all;       /* 1 = EPSV ALL active           */

    /* Transfer modifiers (SITE toggles) */
    int             trailing;       /* SITE TRAILING (0=keep blanks) */
    int             truncate;       /* SITE TRUNCATE                 */
    int             rdw;            /* SITE RDW                      */

    /* Transfer state */
    long            rest_offset;    /* REST restart offset           */
    char            rnfr_path[46];  /* RNFR pending rename source    */

    /* Command buffer */
    char            cmd[FTPD_MAX_CMD_LEN]; /* current command line   */
    int             cmdlen;         /* command length                 */

    /* Idle tracking (heap — immune to SVC stack corruption) */
    time_t          idle_start;     /* getline idle timer start       */

    /* Statistics */
    long            bytes_sent;
    long            bytes_recv;
    int             xfer_count;

    /* Back-pointer to server */
    ftpd_server_t   *server;
};

/*
** Allocate and initialize a new session.
** Returns NULL on failure.
*/
ftpd_session_t *ftpd_session_new(ftpd_server_t *server, int sock)
                                                            asm("FTPSESNW");

/*
** Main session loop (thread entry point).
** Runs the command-response loop until QUIT or error.
*/
int ftpd_session_run(void *udata, CTHDWORK *work)           asm("FTPSESRN");

/*
** Free session resources.
*/
void ftpd_session_free(ftpd_session_t *sess)                asm("FTPSESFR");

/*
** Send an FTP reply on the control connection.
** Message is in EBCDIC internally, translated to ASCII before sending.
*/
void ftpd_session_reply(ftpd_session_t *sess, int code,
                        const char *fmt, ...)               asm("FTPSESRP");

/*
** Send a multi-line FTP reply.
** First line uses "code-" prefix, last line uses "code " prefix.
*/
void ftpd_session_reply_multi(ftpd_session_t *sess, int code,
                              const char *first,
                              const char *last)             asm("FTPSESRM");

/*
** Read one command line from the control connection.
** ASCII -> EBCDIC translation is performed.
** Returns length of command, or -1 on error/disconnect.
*/
int ftpd_session_getline(ftpd_session_t *sess)              asm("FTPSESGL");

#endif /* FTPD_SES_H */
