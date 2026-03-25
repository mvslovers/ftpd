#ifndef FTPD_CMD_H
#define FTPD_CMD_H
/*
** FTPD Command Parser & Dispatcher
*/

/*
** Dispatch an FTP command.
** Returns 0 to continue, -1 to close session.
*/
int ftpd_cmd_dispatch(ftpd_session_t *sess,
                      const char *cmd, const char *arg)     asm("FTPCMDDS");

#endif /* FTPD_CMD_H */
