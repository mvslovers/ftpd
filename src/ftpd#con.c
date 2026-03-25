/*
** FTPD Console Command Handler
**
** Operator MODIFY command parser & dispatcher.
** Follows the UFSD ufsd#cmd.c pattern:
**   - Uppercase + trim CIB data
**   - Flat command dispatch via strcmp/strncmp
**   - WTO responses with FTPDnnnX message IDs
**
** Console commands:
**   /F FTPD,STATS       - display server statistics
**   /F FTPD,SESSIONS    - display active sessions
**   /F FTPD,CONFIG      - display configuration
**   /F FTPD,VERSION     - display version
**   /F FTPD,TRACE ON    - enable trace
**   /F FTPD,TRACE OFF   - disable trace
**   /F FTPD,TRACE DUMP  - dump trace ring buffer
**   /F FTPD,HELP        - show command list
**   /F FTPD,SHUTDOWN    - graceful shutdown
**   /P FTPD             - stop (graceful shutdown)
*/
#include "ftpd.h"

/* Forward declarations for command handlers */
static void cmd_stats(ftpd_server_t *server);
static void cmd_sessions(ftpd_server_t *server);
static void cmd_config(ftpd_server_t *server);
static void cmd_version(ftpd_server_t *server);
static void cmd_trace(ftpd_server_t *server, const char *arg);
static void cmd_help(ftpd_server_t *server);
static void cmd_shutdown(ftpd_server_t *server);

/* ====================================================================
** ftpd_process_cib -- Process a Console Information Block
**
** Called from the main event loop for each CIB.
** Returns 0 on success.
** ================================================================= */
int
ftpd_process_cib(ftpd_server_t *server, CIB *cib)
{
    char buf[128];
    int len;
    int i;
    char *arg;

    switch (cib->cibverb) {

    case CIBSTOP:
        /* /P FTPD */
        cmd_shutdown(server);
        break;

    case CIBMODFY:
        /* /F FTPD,command ... */
        len = (int)cib->cibdatln;
        if (len <= 0 || len >= (int)sizeof(buf))
            break;

        memcpy(buf, cib->cibdata, len);
        buf[len] = '\0';

        /* Uppercase and trim trailing spaces */
        for (i = 0; i < len; i++)
            buf[i] = (char)toupper((unsigned char)buf[i]);
        while (len > 0 && buf[len - 1] == ' ')
            buf[--len] = '\0';

        /* Skip leading spaces */
        arg = buf;
        while (*arg == ' ')
            arg++;

        /* Dispatch commands */
        if (strcmp(arg, "STATS") == 0) {
            cmd_stats(server);
        }
        else if (strcmp(arg, "SESSIONS") == 0) {
            cmd_sessions(server);
        }
        else if (strcmp(arg, "CONFIG") == 0) {
            cmd_config(server);
        }
        else if (strcmp(arg, "VERSION") == 0) {
            cmd_version(server);
        }
        else if (strncmp(arg, "TRACE ", 6) == 0) {
            cmd_trace(server, arg + 6);
        }
        else if (strcmp(arg, "TRACE") == 0) {
            cmd_trace(server, "");
        }
        else if (strcmp(arg, "HELP") == 0) {
            cmd_help(server);
        }
        else if (strcmp(arg, "SHUTDOWN") == 0) {
            cmd_shutdown(server);
        }
        else {
            ftpd_log_wto("FTPD090E Unknown command: %s", arg);
        }
        break;

    default:
        break;
    }

    return 0;
}

/* ====================================================================
** STATS -- display server statistics
** ================================================================= */
static void
cmd_stats(ftpd_server_t *server)
{
    ftpd_log_wto("FTPD010I STATUS: %s",
                 (server->flags & FTPD_ACTIVE) ? "ACTIVE" : "INACTIVE");
    ftpd_log_wto("FTPD011I SESSIONS:    %d active, %ld total",
                 server->num_sessions, server->total_sessions);
    ftpd_log_wto("FTPD012I BYTES IN:    %ld", server->total_bytes_in);
    ftpd_log_wto("FTPD013I BYTES OUT:   %ld", server->total_bytes_out);
    ftpd_log_wto("FTPD014I TRACE:       %s",
                 ftpd_trace_enabled() ? "ON" : "OFF");
}

/* ====================================================================
** SESSIONS -- display active session count
** ================================================================= */
static void
cmd_sessions(ftpd_server_t *server)
{
    ftpd_log_wto("FTPD015I Active sessions: %d / %d",
                 server->num_sessions, server->config.max_sessions);
}

/* ====================================================================
** CONFIG -- display configuration
** ================================================================= */
static void
cmd_config(ftpd_server_t *server)
{
    ftpdcfg_dump(&server->config);
}

/* ====================================================================
** VERSION -- display version string
** ================================================================= */
static void
cmd_version(ftpd_server_t *server)
{
    (void)server;
    ftpd_log_wto("FTPD016I %s", FTPD_VERSION_STR);
}

/* ====================================================================
** TRACE ON|OFF|DUMP -- trace ring buffer control
** ================================================================= */
static void
cmd_trace(ftpd_server_t *server, const char *arg)
{
    (void)server;

    /* Skip leading spaces in argument */
    while (*arg == ' ')
        arg++;

    if (strcmp(arg, "ON") == 0) {
        ftpd_trace_enable(1);
        ftpd_log_wto("FTPD080I Trace enabled");
    }
    else if (strcmp(arg, "OFF") == 0) {
        ftpd_trace_enable(0);
        ftpd_log_wto("FTPD081I Trace disabled");
    }
    else if (strcmp(arg, "DUMP") == 0) {
        int n = ftpd_trace_dump();
        ftpd_log_wto("FTPD082I Trace dumped, %d entries", n);
    }
    else {
        ftpd_log_wto("FTPD090E TRACE: expected ON, OFF, or DUMP");
    }
}

/* ====================================================================
** HELP -- display available commands
** ================================================================= */
static void
cmd_help(ftpd_server_t *server)
{
    (void)server;
    ftpd_log_wto("FTPD020I Commands: STATS, SESSIONS, CONFIG, "
                 "VERSION, TRACE, HELP, SHUTDOWN");
}

/* ====================================================================
** SHUTDOWN -- initiate graceful shutdown
** ================================================================= */
static void
cmd_shutdown(ftpd_server_t *server)
{
    ftpd_log_wto("FTPD097I FTPD shutting down...");
    server->flags &= ~FTPD_ACTIVE;
    server->flags |= FTPD_QUIESCE;
    server->wakeup_ecb = 0x40000000U;
}
