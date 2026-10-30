#ifndef FTPD_ANM_H
#define FTPD_ANM_H
/*
** FTPD ACEE user name check
**
** Free of project and MVS headers on purpose: the comparison is pure, and a
** host unit test can link it (test/tstanm.c).
*/

/*
** Does the user name in an ACEE belong to the userid a client logged in as?
**
** aceeuser is the ACEEUSER field as the ACEE holds it: a length byte
** followed by the name, blank padded.  userid is the name the client sent.
** Both sides are compared in upper case, userid cut to 8 characters (the
** length of a RACF userid, and what racf_login() hands to RACINIT).
**
** Returns 1 when the names are equal, 0 when they differ or either is empty.
**
** The ACEE a login returns has to be the one of the user who was asked for;
** an ACEE of any other user is not a valid result for this login.
*/
int ftpd_acee_user_matches(const char *aceeuser, const char *userid)
                                                    asm("FTPACEUM");

/*
** The name in an ACEEUSER field as a C string, without the padding, for a
** log line.  out holds at least 9 bytes.  An empty name gives "".
*/
void ftpd_acee_user_name(char *out, const char *aceeuser)
                                                    asm("FTPACEUN");

#endif /* FTPD_ANM_H */
