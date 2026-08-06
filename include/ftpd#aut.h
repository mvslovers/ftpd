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

/* racf_auth() return code: no profile covers the resource.  SAF calls this
** "resource not protected"; it is an ALLOW, not a denial — the same answer
** data set OPEN, IDCAMS and the catalog act on. */
#define FTPD_RACF_NOTPROT       4

/*
** True if a racf_auth() return code means the access may proceed: 0 (a
** profile permits it) or 4 (no profile covers the resource).  Everything
** else — 8 and up — is a refusal.
**
** Testing rc != 0 alone is wrong and was only ever harmless by accident:
** libc370 set RACHECK flag byte 0x10 believing it meant LOG=NONE, when it
** is DSTYPE=V, and with that flag RAKF answered 0 where it should answer 4.
** With the flag corrected (libc370 #63) the 4 becomes visible, so both call
** sites must accept it or every unprotected resource turns into a denial —
** for FACILITY/FTPAUTH that means refusing every login on a system without
** that profile, which doc/FTPD_RAKF_SETUP.md §3.4 documents as allowed.
*/
int ftpd_racf_allowed(int rc)                       asm("FTPRACOK");

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
