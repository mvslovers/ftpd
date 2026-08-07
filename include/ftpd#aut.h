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
** Enter also takes an ENQ on server->acee_lock, so only one window is open
** in the address space at a time (#79).  One field cannot hold two
** identities: without it, a worker dispatching inside another session's
** window authorizes as that session's user, which for a pre-checked
** operation means a spurious denial — S913 out of OPEN, i.e. an ABEND and
** a recovery, for what should be a wait of a few milliseconds.
**
** INVARIANT, load-bearing for deadlock freedom: a session must not enter a
** window while holding a data set ENQ (an open DCB or an allocated DD).
** Every window today opens or allocates INSIDE itself and releases before
** the next one — RETR/STOR fopen and then transfer outside the window, MKD
** frees its DD right after, DELE/RMD/RNTO hold nothing.  So a worker inside
** a window never waits on a resource another worker can only release after
** acquiring this ENQ.  Keep it that way: a window opened around an already
** open data set can deadlock the address space.
**
** Both are no-ops for an unauthenticated session (sess->acee == NULL), and
** must be used in matched pairs around the shortest possible window.  They
** must NOT nest: the ENQ is not recursive, so an inner leave() would end
** the outer window's exclusivity.
*/
void ftpd_acee_enter(ftpd_session_t *sess)          asm("FTPACEEN");
void ftpd_acee_leave(ftpd_session_t *sess)          asm("FTPACELV");

#endif /* FTPD_AUT_H */
