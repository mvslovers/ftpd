#ifndef FTPD_CFG_H
#define FTPD_CFG_H
/*
** FTPD Configuration
*/

#include "ftpd#adr.h"               /* address parameter parsing     */

/* --- DASD volume entry --- */
typedef struct ftpd_dasd {
    char            volser[7];      /* volume serial                 */
    char            unit[5];        /* device type (3350, 3380, etc) */
} ftpd_dasd_t;

#define FTPD_MAX_DASD       32      /* max configured DASD volumes   */

/* --- Server configuration --- */
/*
** The three address fields below are all written by ftpd_adr_parse(), so
** they hold either the literal "ANY" or a validated dotted quad -- never a
** value the socket layer would have to guess about.  What ANY means is per
** field, see the comments.
*/
typedef struct ftpd_config {
    /* Network */
    int             port;           /* listen port (default 21)      */
    char            bind_ip[FTPD_ADR_SIZE];
                                    /* control listener bind address,
                                    ** SRVBIND (alias SRVIP).
                                    ** "ANY" = 0.0.0.0                */
    int             bind_ip_alias;  /* 1 = set through the old SRVIP
                                    ** spelling; the CONFIG dump says so */
    char            pasv_addr[FTPD_ADR_SIZE];
                                    /* address the client is told to
                                    ** connect to, PASVADR.  "ANY" =
                                    ** take it from the control
                                    ** connection, per session        */
    char            pasv_bind[FTPD_ADR_SIZE];
                                    /* passive listener bind address,
                                    ** PASVBIND.  "ANY" = 0.0.0.0.
                                    ** NOT the same as pasv_addr: this
                                    ** is where FTPD listens, that is
                                    ** what the client is told to
                                    ** connect to                     */
    int             pasv_lo;        /* PASV port range low           */
    int             pasv_hi;        /* PASV port range high          */
    int             bind_tries;     /* BINDTRIES: how many times to
                                    ** retry a bind() that failed for
                                    ** a reason waiting can fix       */
    int             bind_wait;      /* BINDWAIT: seconds between those
                                    ** retries.  Both matter more since
                                    ** a bind that never succeeds ends
                                    ** the STC (#111) instead of
                                    ** leaving it idle                */
    /* Limits */
    int             max_sessions;   /* max concurrent sessions       */
    int             idle_timeout;   /* idle timeout in seconds       */
    char            banner[80];     /* custom 220 banner text        */

    /* Security */
    char            authuser[9];    /* user allowed to TERM server   */
    int             sslproxy;       /* 1 = acknowledge PBSZ/PROT with
                                    ** 200 although FTPD encrypts
                                    ** nothing.  Only correct behind a
                                    ** TLS terminating proxy -- see
                                    ** SSLPROXY in the sample config  */

    /* JES */
    int             jes_level;      /* default JES interface level   */

    /* Default allocation parameters */
    struct {
        char        recfm[4];
        int         lrecl;
        int         blksize;
        char        unit[5];
        char        volume[7];
    } defaults;

    /* DASD volumes for VTOC scanning */
    int             num_dasd;
    ftpd_dasd_t     dasd[FTPD_MAX_DASD];
} ftpd_config_t;

/*
** Load configuration from DD:FTPDPRM.
** If the DD is not allocated, defaults are used (returns 0).
*/
int ftpdcfg_load(ftpd_config_t *cfg)                        asm("FTPCFGLD");

/*
** Set default values in config structure.
*/
void ftpdcfg_defaults(ftpd_config_t *cfg)                   asm("FTPCFGDF");

/*
** Dump configuration to log (for CONFIG console command).
*/
void ftpdcfg_dump(const ftpd_config_t *cfg)                 asm("FTPCFGDP");

#endif /* FTPD_CFG_H */
