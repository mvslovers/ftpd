#ifndef FTPD_UFS_H
#define FTPD_UFS_H
/*
** FTPD UFS Operations — UFSD Client Integration
**
** Wrapper layer around the UFSD client library (libufs).
** UFSD is a soft dependency: if not running, all UFS commands
** return 550.  Availability is detected lazily per session.
**
** Reference: mvsmf/src/ussapi.c (uss_get_ufs pattern)
**
** Codepage note: UFS stores files in IBM-1047, not CP037.
** Text translation uses ftpd_xlat_a2e/e2a (IBM-1047 tables),
** NOT ftpd_xlat_mvs_a2e/e2a (CP037 — those are for MVS datasets).
*/

/*
** Get per-session UFS handle (lazy init).
** On first call: ufsnew() + ufs_setuser().
** Returns handle on success, NULL if UFSD not running.
** Sends 550 reply on failure.
*/
struct libufs_ufs *ftpd_ufs_get(ftpd_session_t *sess)      asm("FTPUFGET");

/*
** Free per-session UFS handle.
** Called from ftpd_session_free().  Safe to call with NULL handle.
*/
void ftpd_ufs_free(ftpd_session_t *sess)                   asm("FTPUFFRE");

/*
** Map UFSD return code to FTP reply code.
*/
int ftpd_ufs_rc_to_ftp(int ufsd_rc)                        asm("FTPUFRC");

/*
** Get human-readable error message for UFSD return code.
*/
const char *ftpd_ufs_rc_message(int ufsd_rc)                asm("FTPUFMSG");

/*
** Send FTP error reply for a UFSD return code.
** Maps RC to FTP code + message, sends via ftpd_session_reply().
*/
void ftpd_ufs_error(ftpd_session_t *sess, int ufsd_rc)     asm("FTPUFERR");

/*
** Resolve a path argument against the session's UFS CWD.
** Handles relative paths, ".", "..", absolute paths.
** Prevents traversal above UFS root "/".
** Returns 0 on success (result in out), -1 on error.
*/
int ftpd_ufs_resolve(ftpd_session_t *sess, const char *arg,
                     char *out, int outlen)                 asm("FTPUFRES");

/*
** CWD — change UFS working directory.
** Validates path via ufs_chgdir(), updates sess->ufs_cwd.
*/
int ftpd_ufs_cwd(ftpd_session_t *sess, const char *arg)    asm("FTPUFCWD");

/*
** CDUP — move to parent directory in UFS.
*/
int ftpd_ufs_cdup(ftpd_session_t *sess)                    asm("FTPUFCDU");

/*
** LIST/NLST — directory listing on data connection.
** nlst=0: long format (drwxr-xr-x ...), nlst=1: names only.
*/
int ftpd_ufs_list(ftpd_session_t *sess, const char *arg,
                  int nlst)                                 asm("FTPUFLST");

/*
** RETR — send file to client via data connection.
** TYPE A: EBCDIC-1047 → ASCII translation.
** TYPE I: no translation.
*/
int ftpd_ufs_retr(ftpd_session_t *sess, const char *arg)   asm("FTPUFRET");

/*
** STOR — receive file from client via data connection.
** TYPE A: ASCII → EBCDIC-1047 translation.
** TYPE I: no translation.
*/
int ftpd_ufs_stor(ftpd_session_t *sess, const char *arg)   asm("FTPUFSTO");

/*
** DELE — delete a UFS file.
*/
int ftpd_ufs_dele(ftpd_session_t *sess, const char *arg)   asm("FTPUFDEL");

/*
** MKD — create a UFS directory.
*/
int ftpd_ufs_mkd(ftpd_session_t *sess, const char *arg)    asm("FTPUFMKD");

/*
** RMD — remove an empty UFS directory.
*/
int ftpd_ufs_rmd(ftpd_session_t *sess, const char *arg)    asm("FTPUFRMD");

/*
** RNFR — store rename source path.
*/
int ftpd_ufs_rnfr(ftpd_session_t *sess, const char *arg)   asm("FTPUFRNF");

/*
** RNTO — execute rename (not supported by libufs → 502).
*/
int ftpd_ufs_rnto(ftpd_session_t *sess, const char *arg)   asm("FTPUFRNT");

/*
** SIZE — return file size.
*/
int ftpd_ufs_size(ftpd_session_t *sess, const char *arg)   asm("FTPUFSIZ");

/*
** MDTM — return file modification time (YYYYMMDDHHMMSS).
*/
int ftpd_ufs_mdtm(ftpd_session_t *sess, const char *arg)   asm("FTPUFMDT");

#endif /* FTPD_UFS_H */
