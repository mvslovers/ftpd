#ifndef FTPDCFG_H
#define FTPDCFG_H
/*
** FTPD Configuration
*/

/* --- DASD volume entry --- */
typedef struct ftpd_dasd {
    char            volser[7];      /* volume serial                 */
    char            unit[5];        /* device type (3350, 3380, etc) */
} ftpd_dasd_t;

#define FTPD_MAX_DASD       32      /* max configured DASD volumes   */

/* --- Server configuration --- */
typedef struct ftpd_config {
    /* Network */
    int             port;           /* listen port (default 21)      */
    char            bind_ip[16];    /* bind IP ("ANY" = 0.0.0.0)    */
    char            pasv_addr[16];  /* PASV address for responses    */
    int             pasv_lo;        /* PASV port range low           */
    int             pasv_hi;        /* PASV port range high          */
    int             insecure;       /* allow non-localhost connects  */

    /* Limits */
    int             max_sessions;   /* max concurrent sessions       */
    int             idle_timeout;   /* idle timeout in seconds       */
    char            banner[80];     /* custom 220 banner text        */

    /* Security */
    char            authuser[9];    /* user allowed to TERM server   */

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
** Load configuration from a dataset.
** If dsname is NULL, uses SYS1.PARMLIB(FTPDPM00).
** Returns 0 on success, -1 on error.
*/
int ftpdcfg_load(ftpd_config_t *cfg, const char *dsname);

/*
** Set default values in config structure.
*/
void ftpdcfg_defaults(ftpd_config_t *cfg);

/*
** Dump configuration to log (for D CONFIG console command).
*/
void ftpdcfg_dump(const ftpd_config_t *cfg);

#endif /* FTPDCFG_H */
