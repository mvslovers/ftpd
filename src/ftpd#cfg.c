/*
** FTPD Configuration Parser
**
** Reads key=value configuration from SYS1.PARMLIB(FTPDPM00) or a
** PARM-specified dataset. Supports comments (#) and DASD volume lines.
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
    strcpy(cfg->bind_ip, "ANY");
    strcpy(cfg->pasv_addr, "127.0.0.1");
    cfg->pasv_lo = 22000;
    cfg->pasv_hi = 22200;
    cfg->insecure = 0;

    /* Limits */
    cfg->max_sessions = 10;
    cfg->idle_timeout = 300;
    strcpy(cfg->banner, "MVS 3.8j FTPD Server");

    /* Security */
    memset(cfg->authuser, 0, sizeof(cfg->authuser));

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
    else if (strcmp(key, "SRVIP") == 0) {
        strncpy(cfg->bind_ip, value, sizeof(cfg->bind_ip) - 1);
    }
    else if (strcmp(key, "PASVADR") == 0) {
        /* Accept comma-separated: 127,0,0,1 -> 127.0.0.1 */
        char buf[16];
        int i;
        strncpy(buf, value, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        for (i = 0; buf[i]; i++) {
            if (buf[i] == ',')
                buf[i] = '.';
        }
        strcpy(cfg->pasv_addr, buf);
    }
    else if (strcmp(key, "PASVPORTS") == 0) {
        /* Format: low-high */
        char *dash = strchr(value, '-');
        if (dash) {
            cfg->pasv_lo = atoi(value);
            cfg->pasv_hi = atoi(dash + 1);
        }
    }
    else if (strcmp(key, "INSECURE") == 0) {
        cfg->insecure = atoi(value);
    }
    else if (strcmp(key, "MAXSESSIONS") == 0) {
        cfg->max_sessions = atoi(value);
        if (cfg->max_sessions < 1)
            cfg->max_sessions = 1;
    }
    else if (strcmp(key, "IDLETIMEOUT") == 0) {
        cfg->idle_timeout = atoi(value);
    }
    else if (strcmp(key, "BANNER") == 0) {
        strncpy(cfg->banner, value, sizeof(cfg->banner) - 1);
    }
    else if (strcmp(key, "AUTHUSER") == 0) {
        strncpy(cfg->authuser, value, sizeof(cfg->authuser) - 1);
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
** Load configuration from a dataset.
** ----------------------------------------------------------------- */
int
ftpdcfg_load(ftpd_config_t *cfg, const char *dsname)
{
    FILE *fp;
    char line[256];
    const char *name;

    ftpdcfg_defaults(cfg);

    name = dsname ? dsname : "SYS1.PARMLIB(FTPDPM00)";

    ftpd_log(LOG_INFO, "%s: loading configuration from %s", __func__, name);

    fp = fopen(name, "r");
    if (!fp) {
        ftpd_log(LOG_WARN, "%s: cannot open config %s, using defaults",
                 __func__, name);
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

    ftpd_log_wto("FTPD040I Configuration:");
    ftpd_log_wto("FTPD041I   SRVPORT=%d SRVIP=%s", cfg->port, cfg->bind_ip);
    ftpd_log_wto("FTPD042I   PASVADR=%s PASVPORTS=%d-%d",
                 cfg->pasv_addr, cfg->pasv_lo, cfg->pasv_hi);
    ftpd_log_wto("FTPD043I   MAXSESSIONS=%d IDLETIMEOUT=%d",
                 cfg->max_sessions, cfg->idle_timeout);
    ftpd_log_wto("FTPD044I   BANNER=%s", cfg->banner);
    ftpd_log_wto("FTPD045I   DEFRECFM=%s DEFLRECL=%d DEFBLKSIZE=%d",
                 cfg->defaults.recfm, cfg->defaults.lrecl,
                 cfg->defaults.blksize);
    ftpd_log_wto("FTPD046I   DEFUNIT=%s DEFVOLUME=%s",
                 cfg->defaults.unit, cfg->defaults.volume);
    ftpd_log_wto("FTPD047I   DASD volumes=%d:", cfg->num_dasd);
    for (i = 0; i < cfg->num_dasd; i++) {
        ftpd_log_wto("FTPD048I     %s,%s",
                     cfg->dasd[i].volser, cfg->dasd[i].unit);
    }
}
