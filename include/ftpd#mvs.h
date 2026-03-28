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
** CDUP — remove last qualifier from CWD.
** SYS1.MACLIB. → SYS1.
*/
int ftpd_mvs_cdup(ftpd_session_t *sess)             asm("FTPMVCDU");

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

/*
** RETR — send dataset/member to client via data connection.
*/
int ftpd_mvs_retr(ftpd_session_t *sess, const char *arg)
                                                    asm("FTPMVRET");

/*
** STOR — receive data from client, write to dataset/member.
*/
int ftpd_mvs_stor(ftpd_session_t *sess, const char *arg)
                                                    asm("FTPMVSTO");

/*
** APPE — append data to existing dataset (create if not exists).
*/
int ftpd_mvs_appe(ftpd_session_t *sess, const char *arg)
                                                    asm("FTPMVAPP");

/*
** DELE — delete dataset or PDS member.
*/
int ftpd_mvs_dele(ftpd_session_t *sess, const char *arg)
                                                    asm("FTPMVDEL");

/*
** MKD — create a new PDS.
*/
int ftpd_mvs_mkd(ftpd_session_t *sess, const char *arg)
                                                    asm("FTPMVMKD");

/*
** RMD — remove (scratch) a PDS.
*/
int ftpd_mvs_rmd(ftpd_session_t *sess, const char *arg)
                                                    asm("FTPMVRMD");

/*
** RNFR — store rename source, check existence.
*/
int ftpd_mvs_rnfr(ftpd_session_t *sess, const char *arg)
                                                    asm("FTPMVRNF");

/*
** RNTO — execute rename from rnfr_path to new name.
*/
int ftpd_mvs_rnto(ftpd_session_t *sess, const char *arg)
                                                    asm("FTPMVRNT");

#endif /* FTPD_MVS_H */
