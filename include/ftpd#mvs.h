#ifndef FTPD_MVS_H
#define FTPD_MVS_H
/*
** FTPD MVS Dataset Operations
**
** Catalog-based dataset access using crent370 functions:
** __listds(), __listpd(), __locate(), __dscbdv()
*/
#include "cliblist.h"
#include "clibdscb.h"

/*
** Resolve CWD path.
** Quoted ('DSN') = absolute. Unquoted = relative to HLQ.
** Sets sess->mvs_cwd. Returns 0 on success, -1 on error.
*/
int ftpd_mvs_cwd(ftpd_session_t *sess, const char *arg)
                                                    asm("FTPMVCWD");

/*
** Check if a dataset name refers to a PDS.
** Uses __locate() + __dscbdv() to read DSORG.
** Returns 1 if PDS, 0 if not, -1 on error.
*/
int ftpd_mvs_is_pds(const char *dsn)               asm("FTPMVPDS");

/*
** List datasets matching the current CWD prefix.
** Sends formatted listing on the data connection.
** Returns 0 on success, -1 on error.
*/
int ftpd_mvs_list(ftpd_session_t *sess, const char *arg, int nlst)
                                                    asm("FTPMVLST");

/*
** Get dataset size (used tracks * blksize approximation).
** Returns size in bytes, or -1 on error.
*/
long ftpd_mvs_size(ftpd_session_t *sess, const char *dsn)
                                                    asm("FTPMVSIZ");

#endif /* FTPD_MVS_H */
