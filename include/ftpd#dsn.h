#ifndef FTPD_DSN_H
#define FTPD_DSN_H
/*
** FTPD Data Set Name Validation
**
** One question, asked before a name reaches the catalog: is this a data set
** name at all?  It used to be asked by nobody -- a client that sent its local
** path as the remote name (`put build/ftpd.deploy.xmit`) had it turned into
** HERC01.BUILD/FTPD.DEPLOY.XMIT, and the '/' was only noticed by SVC 99,
** which reported an allocation failure for a name that was never allocatable
** (issue #95).
**
** Free of project and MVS headers on purpose: this is the piece of the name
** handling that a host unit test can link (test/tstdsn.c).
*/

/*
** Is dsn a syntactically valid MVS data set name?
**
** Checked: every character.  Qualifiers are separated by '.' and may hold
** A-Z, 0-9, the national characters @ # $, and the hyphen '-'.  The name is
** expected to be uppercase already -- resolve_dsn() uppercases before asking,
** and a lowercase letter in a name that reached this point is a bug worth
** hearing about rather than a name to fix up silently.
**
** A member -- everything from the first '(' -- is not looked at.  Member
** names are sanitized on their own path (sanitize_member() in ftpd#mvs.c),
** and the callers still hold the combined DSN(MEMBER) form when they ask.
**
** Deliberately NOT checked, so that names MVS accepts today keep working:
** qualifier length (<= 8), empty qualifiers, and the rule that a qualifier
** begins with an alphabetic or national character.  The length rule would
** turn `ls ABCDEFGHIJ*` -- a pattern that simply matches nothing -- into a
** 501, and MVS 3.8j is more permissive about the first character than the
** manuals are.  Both are separate decisions; see issue #95.
**
** allow_wildcards -- when nonzero, '*' and '%' are accepted as ordinary
**                    characters.  LIST and NLST resolve patterns, every
**                    other command resolves one name.
**
** Returns 1 when the name is valid, 0 when it is not.  A NULL or empty name
** is not valid.
*/
int ftpd_dsn_valid(const char *dsn, int allow_wildcards)     asm("FTPDSNVL");

#endif /* FTPD_DSN_H */
