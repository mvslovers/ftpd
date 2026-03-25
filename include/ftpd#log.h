#ifndef FTPD_LOG_H
#define FTPD_LOG_H
/*
** FTPD Logging
**
** WTO for important events (startup, shutdown, auth failures, errors).
** STDOUT for detailed logging (transfers, debug, session activity).
** Trace ring buffer for diagnostic capture.
*/

/* --- Log levels --- */
#define LOG_ERROR           0
#define LOG_WARN            1
#define LOG_INFO            2
#define LOG_DEBUG           3

/*
** Write a message to the operator console (WTO).
** Use for important events only: startup, shutdown, errors, console responses.
** Format: FTPDnnnX message
*/
void ftpd_log_wto(const char *fmt, ...)                     asm("FTPLOGW");

/*
** Write a log message to STDOUT with timestamp and level.
** This is the general-purpose logging function.
*/
void ftpd_log(int level, const char *fmt, ...)              asm("FTPLOG");

/*
** Set the current log level. Messages above this level are suppressed.
** Default: LOG_INFO
*/
void ftpd_log_set_level(int level)                          asm("FTPLOGLV");

/*
** Get the current log level.
*/
int ftpd_log_get_level(void)                                asm("FTPLOGGL");

/* --- Trace ring buffer --- */

/*
** Initialize the trace ring buffer.
** size = number of entries (each entry is a fixed-size string).
** Returns 0 on success, -1 on failure.
*/
int ftpd_trace_init(int size)                               asm("FTPTRINI");

/*
** Free the trace ring buffer.
*/
void ftpd_trace_free(void)                                  asm("FTPTRFRE");

/*
** Write an entry to the trace ring buffer.
** Only writes if tracing is enabled.
*/
void ftpd_trace(const char *fmt, ...)                       asm("FTPTRACE");

/*
** Enable or disable tracing.
*/
void ftpd_trace_enable(int on)                              asm("FTPTRENB");

/*
** Dump the trace ring buffer contents to STDOUT.
** Returns the number of entries dumped.
*/
int ftpd_trace_dump(void)                                   asm("FTPTRDMP");

/*
** Check if tracing is currently enabled.
*/
int ftpd_trace_enabled(void)                                asm("FTPTRENQ");

#endif /* FTPD_LOG_H */
