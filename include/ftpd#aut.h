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

/*
** Identity switch around MVS services that must authorize against the
** logged-in user (dataset OPEN, SVC 99, IDCAMS, JES internal reader).
**
** racf_set_acee() writes ASXBSENV, which is ADDRESS-SPACE-wide, while FTPD
** runs one TCB per session.  Enter sets the session's ACEE; leave restores
** the STC identity captured at startup — never the value observed on entry.
** Restoring an observed value lets a worker inherit a concurrent session's
** ACEE and leave it in ASXBSENV after both sides "restored" (#64); the AS
** then rests on a user ACEE, and that user's racf_logout() clears ASXBSENV
** to zero — which RAKF treats as "access permitted" (ICHSFR00: no ACEE ->
** RACHGOOD).  Restoring a constant makes both states unreachable.
**
** Both are no-ops for an unauthenticated session (sess->acee == NULL), and
** must be used in matched pairs around the shortest possible window.
*/
void ftpd_acee_enter(ftpd_session_t *sess)          asm("FTPACEEN");
void ftpd_acee_leave(ftpd_session_t *sess)          asm("FTPACELV");

#endif /* FTPD_AUT_H */
