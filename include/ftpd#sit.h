#ifndef FTPD_SIT_H
#define FTPD_SIT_H
/*
** FTPD SITE Command Processing
*/

/*
** Dispatch SITE subcommand.
** arg: everything after "SITE " (e.g. "RECFM=FB").
** Returns 0 on success.
*/
int ftpd_site_dispatch(ftpd_session_t *sess, const char *arg)
                                                    asm("FTPSITDS");

#endif /* FTPD_SIT_H */
