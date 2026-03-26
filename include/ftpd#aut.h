#ifndef FTPD_AUT_H
#define FTPD_AUT_H
/*
** FTPD Authentication via RAKF (crent370 racf module)
*/

/*
** Authenticate user via RAKF.
** Called from the PASS command handler.
**
** - Calls racf_login() to verify userid/password
** - Checks FACILITY class resource FTPAUTH for authorization
** - Sets session state (authenticated, hlq, mvs_cwd, acee)
** - Sends FTP reply (230 on success, 530 on failure)
** - After 3 failed attempts, closes the connection
**
** Returns 0 to continue the session, -1 to close it.
*/
int ftpd_auth_pass(ftpd_session_t *sess, const char *password)
                                                    asm("FTPAUTPS");

#endif /* FTPD_AUT_H */
