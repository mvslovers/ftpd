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
** The characters that make a name a pattern rather than a name.  Shared so
** that the check, the LIST path and the message that names the offending
** qualifier cannot drift apart.  '?' is here because FTPD has always
** documented and rejected it alongside '*' and '%' -- the matcher itself
** (ftpd_dsn_match() in ftpd#lvl.c) implements only '*', '**' and '%', so a
** '?' in a LIST pattern matches nothing.
*/
#define FTPD_DSN_WILDCARDS  "*%?"

/*
** Is dsn a syntactically valid MVS data set name?
**
** Checked: every character, and that no qualifier is empty.  Qualifiers are
** separated by '.' and may hold A-Z, 0-9, the national characters @ # $, and
** the hyphen '-'.  The name is expected to be uppercase already --
** resolve_dsn() uppercases before asking, and a lowercase letter in a name
** that reached this point is a bug worth hearing about rather than a name to
** fix up silently.  A trailing dot counts as an empty qualifier; resolve_dsn()
** strips it before asking, because there it means "prefix only".
**
** A member -- everything from the first '(' -- has to be exactly "(NAME)" at
** the end of the name: NAME holds the characters a qualifier holds and is not
** empty, nothing follows the closing parenthesis, and there is one member
** only.  An extension after a '.' in NAME (`DSN(name.ext)`, from a client
** that sends file names) is accepted but its characters are checked too;
** sanitize_member() in ftpd#mvs.c cuts it off afterwards (#335).  Whether a
** member is acceptable where the name is used -- a CWD sets a prefix and takes
** none -- is for the caller to decide.
**
** Deliberately NOT checked, so that names MVS accepts today keep working:
** qualifier length (<= 8) and the rule that a qualifier begins with an
** alphabetic or national character.  The length rule would turn
** `ls ABCDEFGHIJ*` -- a pattern that simply matches nothing -- into a 501,
** and MVS 3.8j is more permissive about the first character than the manuals
** are.  Both are separate decisions; see issue #95.
**
** allow_wildcards -- when nonzero, FTPD_DSN_WILDCARDS are accepted as
**                    ordinary characters, in the member as well.  LIST and
**                    NLST resolve patterns, every other command resolves one
**                    name.
**
** Returns 1 when the name is valid, 0 when it is not.  A NULL or empty name
** is not valid.
*/
int ftpd_dsn_valid(const char *dsn, int allow_wildcards)     asm("FTPDSNVL");

/*
** Does the resolved name carry a member, DSN(MEMBER)?
**
** For the commands that act on a whole data set -- CWD, MKD and RMD.  They
** have to answer 501 for a name with a member before they ask RACF or the
** catalog anything: neither can be asked about DSN(MEMBER) (a profile is held
** under the data set name, LOCATE ignores the member), and IDCAMS DELETE of
** DSN(MEMBER) removes the member (#339).  Looks for a '(' only; whether the
** rest is a well-formed member is ftpd_dsn_valid()'s question.
**
** Returns 1 when dsn holds a '(', 0 when it does not or is NULL.
*/
int ftpd_dsn_has_member(const char *dsn)                    asm("FTPDSNHM");

#endif /* FTPD_DSN_H */
