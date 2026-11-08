#ifndef FTPD_PRB_H
#define FTPD_PRB_H
/*
** FTPD start check: the outcome of the check login
**
** Free of project and MVS headers on purpose: the decision is pure, and a
** host unit test can link it (test/tstprb.c).
*/

/*
** Did the security system accept the check login of ftpd_auth_probe()?
**
** has_acee is non-zero when racf_login() returned an ACEE, racf_rc is the
** return code it stored.  The check logs in with a userid and password that
** cannot be valid, so a verifying security system refuses the login and
** racf_login() answers NULL with the reason.  On RAKF the reason is 28: the
** userid is not in its table.
**
** The login counts as accepted when
**  - an ACEE came back, whatever the return code says;
**  - no ACEE came back and the return code is 0;
**  - no ACEE came back and the return code is 8, "the password is not
**    authorized".  Since libc370 2.28.0 racf_login() answers so when the
**    security system accepted the login but the ACEE it created is not the
**    one of the user asked for: racf_login() deletes that ACEE and returns
**    NULL (libc370 include/mvs/racf.h).  For the check login that outcome is
**    an acceptance.  A verifying security system has no reason to answer 8
**    to a userid it does not know: RAKF answers 8 only for a userid it found
**    in its table and whose password did not match.
**
** Every other return code without an ACEE is a refusal: the security system
** verifies userids.
**
** Returns 1 when the check login was accepted, 0 when it was refused.
*/
int ftpd_probe_accepted(int has_acee, int racf_rc)  asm("FTPPRBAC");

/* RACINIT return code "the password is not authorized".  Also the code
** libc370 2.28.0 and later stores for a login whose ACEE is not the
** requested user's. */
#define FTPD_RACF_NOTAUTH       8

#endif /* FTPD_PRB_H */
