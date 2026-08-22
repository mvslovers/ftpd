/*
** FTPD Configuration Parser
**
** Reads key=value configuration from DD:FTPDPRM (the FTPDPRM DD card
** in the STC JCL procedure). Supports comments (#) and DASD volume lines.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "ftpd#cfg.h"
#include "ftpd#log.h"

/* --------------------------------------------------------------------
** Default configuration values
** ----------------------------------------------------------------- */
void
ftpdcfg_defaults(ftpd_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));

    /* Network */
    cfg->port = 2121;
    strcpy(cfg->bind_ip, "ANY");    /* listen on every address       */
    cfg->bind_ip_alias = 0;
    strcpy(cfg->pasv_addr, "ANY");  /* advertise the address the client
                                    ** reached us on -- a literal default
                                    ** cannot be right for every client */
    strcpy(cfg->pasv_bind, "ANY");  /* bind every address, as before */
    cfg->pasv_lo = 22000;
    cfg->pasv_hi = 22200;
    cfg->bind_tries = 10;           /* HTTPD's defaults: up to 100s of
                                    ** patience before the STC ends   */
    cfg->bind_wait = 10;
    /* Limits */
    cfg->max_sessions = 10;
    cfg->idle_timeout = 300;
    strcpy(cfg->banner, "MVS 3.8j FTPD Server");

    /* Security */
    memset(cfg->authuser, 0, sizeof(cfg->authuser));
    cfg->sslproxy = 0;              /* off: PBSZ/PROT stay unimplemented */

    /* JES */
    cfg->jes_level = 2;

    /* Default allocation parameters */
    strcpy(cfg->defaults.recfm, "FB");
    cfg->defaults.lrecl = 80;
    cfg->defaults.blksize = 3120;
    strcpy(cfg->defaults.unit, "3390");
    strcpy(cfg->defaults.volume, "PUB001");

    /* DASD */
    cfg->num_dasd = 0;
}

/* --------------------------------------------------------------------
** Trim leading and trailing whitespace in place.
** Returns pointer to the trimmed string (within the original buffer).
** ----------------------------------------------------------------- */
static char *
trim(char *s)
{
    char *end;

    while (*s == ' ' || *s == '\t')
        s++;
    if (*s == '\0')
        return s;

    end = s + strlen(s) - 1;
    while (end > s && (*end == ' ' || *end == '\t' || *end == '\n' ||
           *end == '\r'))
        *end-- = '\0';

    return s;
}

/* --------------------------------------------------------------------
** Try to parse a DASD volume line: "VOLSER,UNIT  comment"
** Returns 1 if parsed successfully, 0 if not a DASD line.
** ----------------------------------------------------------------- */
static int
parse_dasd_line(ftpd_config_t *cfg, const char *line)
{
    char volser[7];
    char unit[5];
    const char *p;
    int i;

    /* DASD lines start with a letter and contain a comma */
    if (!isalpha((unsigned char)line[0]))
        return 0;

    p = strchr(line, ',');
    if (!p)
        return 0;

    /* Extract volser (before comma) */
    i = (int)(p - line);
    if (i < 1 || i > 6)
        return 0;
    memcpy(volser, line, i);
    volser[i] = '\0';

    /* Extract unit (after comma, up to whitespace) */
    p++;
    i = 0;
    while (*p && *p != ' ' && *p != '\t' && i < 4) {
        unit[i++] = *p++;
    }
    unit[i] = '\0';

    if (i == 0)
        return 0;

    /* Add to DASD table */
    if (cfg->num_dasd >= FTPD_MAX_DASD) {
        ftpd_log(LOG_WARN, "%s: DASD table full, ignoring volume %s",
                 __func__, volser);
        return 1;
    }

    strcpy(cfg->dasd[cfg->num_dasd].volser, volser);
    strcpy(cfg->dasd[cfg->num_dasd].unit, unit);
    cfg->num_dasd++;

    return 1;
}

/* --------------------------------------------------------------------
** Parse a key=value line.
** ----------------------------------------------------------------- */
static void
parse_keyvalue(ftpd_config_t *cfg, const char *key, const char *value)
{
    if (strcmp(key, "SRVPORT") == 0) {
        cfg->port = atoi(value);
        if (cfg->port < 1 || cfg->port > 65535) {
            ftpd_log(LOG_WARN, "%s: invalid SRVPORT %s, using default 21",
                     __func__, value);
            cfg->port = 21;
        }
    }
    else if (strcmp(key, "SRVBIND") == 0 || strcmp(key, "SRVIP") == 0) {
        /* Where the control listener binds -- ANY or one address.  SRVIP is
        ** the old spelling, still read so existing PARMLIB members keep
        ** working; SRVBIND is what the dump and the sample call it. */
        if (ftpd_adr_parse(value, cfg->bind_ip, NULL) == FTPD_ADR_BAD) {
            ftpd_log(LOG_WARN, "%s: invalid %s %s, listening on every "
                     "address (ANY)", __func__, key, value);
            strcpy(cfg->bind_ip, "ANY");
        }
        cfg->bind_ip_alias = (strcmp(key, "SRVIP") == 0);
    }
    else if (strcmp(key, "BINDTRIES") == 0 ||
             strcmp(key, "BINDWAIT") == 0) {
        /* How long to keep trying a bind that failed for a reason waiting
        ** can fix.  Clamped rather than refused: an out-of-range value here
        ** is a typo, and the nearest legal one is what was meant.  100 is
        ** the ceiling on both, so the longest possible wait is bounded --
        ** the operator can always /P, the retry checks for it every second.
        **
        ** HTTPD spells these BIND_TRIES and BIND_SLEEP with the same
        ** defaults; the underscore is dropped here because every other FTPD
        ** keyword is written without one (SRVPORT, PASVPORTS, MAXSESSIONS).
        */
        int v = atoi(value);

        if (v < 1 || v > 100) {
            int clamped = (v < 1) ? 1 : 100;
            ftpd_log(LOG_WARN, "%s: %s %s out of range 1-100, using %d",
                     __func__, key, value, clamped);
            v = clamped;
        }
        if (strcmp(key, "BINDTRIES") == 0)
            cfg->bind_tries = v;
        else
            cfg->bind_wait = v;
    }
    else if (strcmp(key, "PASVADR") == 0) {
        /* What the client is told to connect to.  ANY -- and anything
        ** unreadable -- means the address the client reached us on, which
        ** the session resolves from the control connection. */
        if (ftpd_adr_parse(value, cfg->pasv_addr, NULL) == FTPD_ADR_BAD) {
            ftpd_log(LOG_WARN, "%s: invalid PASVADR %s, using the control "
                     "connection address", __func__, value);
            strcpy(cfg->pasv_addr, "ANY");
        }
    }
    else if (strcmp(key, "PASVBIND") == 0) {
        /* Where the passive listener binds -- ANY or one address.  A
        ** co-located TLS proxy owns the public address on the same
        ** ports, so FTPD must be able to stay off it. */
        if (ftpd_adr_parse(value, cfg->pasv_bind, NULL) == FTPD_ADR_BAD) {
            ftpd_log(LOG_WARN, "%s: invalid PASVBIND %s, binding every "
                     "address (ANY)", __func__, value);
            strcpy(cfg->pasv_bind, "ANY");
        }
    }
    else if (strcmp(key, "PASVPORTS") == 0) {
        /* Format: low-high */
        char *dash = strchr(value, '-');
        if (dash) {
            cfg->pasv_lo = atoi(value);
            cfg->pasv_hi = atoi(dash + 1);
        }
    }
    else if (strcmp(key, "MAXSESSIONS") == 0) {
        cfg->max_sessions = atoi(value);
        if (cfg->max_sessions < 1)
            cfg->max_sessions = 1;
    }
    else if (strcmp(key, "IDLETIMEOUT") == 0) {
        cfg->idle_timeout = atoi(value);
        if (cfg->idle_timeout <= 0) {
            ftpd_log(LOG_WARN, "%s: IDLETIMEOUT must be > 0, using 300",
                     __func__);
            cfg->idle_timeout = 300;
        }
    }
    else if (strcmp(key, "BANNER") == 0) {
        strncpy(cfg->banner, value, sizeof(cfg->banner) - 1);
    }
    else if (strcmp(key, "AUTHUSER") == 0) {
        strncpy(cfg->authuser, value, sizeof(cfg->authuser) - 1);
    }
    else if (strcmp(key, "SSLPROXY") == 0) {
        /* YES makes FTPD answer PBSZ/PROT with 200 without encrypting
        ** anything -- correct only when a TLS terminating proxy is in
        ** front of it.  Anything but YES leaves the option off. */
        if (strcmp(value, "YES") == 0 || strcmp(value, "yes") == 0) {
            cfg->sslproxy = 1;
        } else if (strcmp(value, "NO") == 0 || strcmp(value, "no") == 0) {
            cfg->sslproxy = 0;
        } else {
            ftpd_log(LOG_WARN, "%s: SSLPROXY must be YES or NO, "
                     "got %s, staying off", __func__, value);
            cfg->sslproxy = 0;
        }
    }
    else if (strcmp(key, "JESINTERFACELEVEL") == 0) {
        cfg->jes_level = atoi(value);
        if (cfg->jes_level < 1 || cfg->jes_level > 2)
            cfg->jes_level = 2;
    }
    else if (strcmp(key, "DEFRECFM") == 0) {
        strncpy(cfg->defaults.recfm, value, sizeof(cfg->defaults.recfm) - 1);
    }
    else if (strcmp(key, "DEFLRECL") == 0) {
        cfg->defaults.lrecl = atoi(value);
    }
    else if (strcmp(key, "DEFBLKSIZE") == 0) {
        cfg->defaults.blksize = atoi(value);
    }
    else if (strcmp(key, "DEFUNIT") == 0) {
        strncpy(cfg->defaults.unit, value, sizeof(cfg->defaults.unit) - 1);
    }
    else if (strcmp(key, "DEFVOLUME") == 0) {
        strncpy(cfg->defaults.volume, value,
                sizeof(cfg->defaults.volume) - 1);
    }
    else {
        ftpd_log(LOG_WARN, "%s: unknown config key: %s", __func__, key);
    }
}

/* --------------------------------------------------------------------
** Parse a single line from the config file.
** ----------------------------------------------------------------- */
static void
parse_line(ftpd_config_t *cfg, char *line)
{
    char *p;
    char *key;
    char *value;

    p = trim(line);

    /* Skip empty lines and comments */
    if (*p == '\0' || *p == '#')
        return;

    /* Try DASD volume line first */
    if (parse_dasd_line(cfg, p))
        return;

    /* Look for key=value */
    key = p;
    value = strchr(p, '=');
    if (!value) {
        ftpd_log(LOG_WARN, "%s: unrecognized config line: %.40s", __func__, p);
        return;
    }

    *value = '\0';
    value++;

    key = trim(key);
    value = trim(value);

    /* Convert key to uppercase for case-insensitive matching */
    for (p = key; *p; p++)
        *p = (char)toupper((unsigned char)*p);

    parse_keyvalue(cfg, key, value);
}

/* --------------------------------------------------------------------
** Load configuration from DD:FTPDPRM.
** If the DD is not allocated, log a warning and use defaults.
** ----------------------------------------------------------------- */
int
ftpdcfg_load(ftpd_config_t *cfg)
{
    FILE *fp;
    char line[256];

    ftpdcfg_defaults(cfg);

    ftpd_log(LOG_INFO, "%s: loading config from DD:FTPDPRM", __func__);

    fp = fopen("DD:FTPDPRM", "r");
    if (!fp) {
        ftpd_log(LOG_WARN, "%s: cannot open DD:FTPDPRM, using defaults",
                 __func__);
        return 0;
    }

    while (fgets(line, sizeof(line), fp)) {
        parse_line(cfg, line);
    }

    fclose(fp);

    ftpd_log(LOG_INFO, "%s: loaded, port=%d, max_sessions=%d, "
             "DASD volumes=%d", __func__,
             cfg->port, cfg->max_sessions, cfg->num_dasd);

    return 0;
}

/* --------------------------------------------------------------------
** Dump configuration (for CONFIG console command).
** ----------------------------------------------------------------- */
void
ftpdcfg_dump(const ftpd_config_t *cfg)
{
    int i;

    ftpd_log_wto("FTPD040I CONFIGURATION:");
    ftpd_log_wto("FTPD041I   SRVPORT=%d SRVBIND=%s%s", cfg->port,
                 cfg->bind_ip,
                 cfg->bind_ip_alias ? " (SET AS SRVIP)" : "");
    /* PASVADR=ANY is not an address the client could use -- say what it
    ** resolves to instead, the operator is reading this to find out why a
    ** client connects where it does. */
    ftpd_log_wto("FTPD042I   PASVADR=%s PASVPORTS=%d-%d PASVBIND=%s",
                 strcmp(cfg->pasv_addr, "ANY") == 0
                     ? "ANY (CONTROL CONNECTION)" : cfg->pasv_addr,
                 cfg->pasv_lo, cfg->pasv_hi,
                 cfg->pasv_bind);
    ftpd_log_wto("FTPD057I   BINDTRIES=%d BINDWAIT=%d",
                 cfg->bind_tries, cfg->bind_wait);
    ftpd_log_wto("FTPD043I   MAXSESSIONS=%d IDLETIMEOUT=%d",
                 cfg->max_sessions, cfg->idle_timeout);
    ftpd_log_wto("FTPD044I   BANNER=%s", cfg->banner);
    ftpd_log_wto("FTPD049I   SSLPROXY=%s",
                 cfg->sslproxy ? "YES" : "NO");
    ftpd_log_wto("FTPD045I   DEFRECFM=%s DEFLRECL=%d DEFBLKSIZE=%d",
                 cfg->defaults.recfm, cfg->defaults.lrecl,
                 cfg->defaults.blksize);
    ftpd_log_wto("FTPD046I   DEFUNIT=%s DEFVOLUME=%s",
                 cfg->defaults.unit, cfg->defaults.volume);
    ftpd_log_wto("FTPD047I   DASD VOLUMES=%d:", cfg->num_dasd);
    for (i = 0; i < cfg->num_dasd; i++) {
        ftpd_log_wto("FTPD048I     %s,%s",
                     cfg->dasd[i].volser, cfg->dasd[i].unit);
    }
}
