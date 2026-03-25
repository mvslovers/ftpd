#ifndef FTPD_DAT_H
#define FTPD_DAT_H
/*
** FTPD Data Connection Management
*/

/*
** Parse PORT command arguments (h1,h2,h3,h4,p1,p2).
** Returns 0 on success, -1 on parse error.
*/
int ftpd_data_port(ftpd_session_t *sess, const char *arg)
                                                            asm("FTPDTPRT");

/*
** Open passive listener, send 227 reply.
** Returns 0 on success, -1 on error.
*/
int ftpd_data_pasv(ftpd_session_t *sess)                    asm("FTPDTPSV");

/*
** Establish data connection (accept for PASV, connect for PORT).
** Returns 0 on success, -1 on error.
*/
int ftpd_data_open(ftpd_session_t *sess)                    asm("FTPDTOPN");

/*
** Close data connection and passive listener.
*/
void ftpd_data_close(ftpd_session_t *sess)                  asm("FTPDTCLS");

/*
** Send data on data connection.
** Returns bytes sent, or -1 on error.
*/
int ftpd_data_send(ftpd_session_t *sess,
                   const void *buf, int len)                asm("FTPDTSND");

/*
** Receive data from data connection.
** Returns bytes received, 0 on EOF, or -1 on error.
*/
int ftpd_data_recv(ftpd_session_t *sess,
                   void *buf, int len)                      asm("FTPDTRCV");

/*
** Send formatted string on data connection with EBCDIC->ASCII translation.
** Returns bytes sent, or -1 on error.
*/
int ftpd_data_printf(ftpd_session_t *sess,
                     const char *fmt, ...)                  asm("FTPDTPRF");

#endif /* FTPD_DAT_H */
