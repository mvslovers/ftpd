#ifndef FTPD_LVL_H
#define FTPD_LVL_H
/*
** FTPD LIST/NLST Data Set Pattern and Catalog Level
**
** LIST 'SYS2.PROCLIB' and LIST 'SYS2.**' from a working directory that is
** not SYS2 answered an empty list: the catalog was read at the level of the
** working directory and the pattern only filtered that result, so a name
** outside the working directory could never be in it.  The Zowe FTP plugin
** sends exactly such names, with no CWD before (#331).
**
** Free of project and MVS headers on purpose, so a host unit test can link it
** (test/tstlvl.c).
*/

/*
** The z/OS-compatible data set name matcher of LIST and NLST.
**
** Qualifier-based, each qualifier separated by '.':
**   *   matches exactly one qualifier (or the rest of one)
**   **  matches any number of qualifiers, also none
**   %   matches exactly one character within a qualifier
**   literal characters match case-insensitively
**
** Both pattern and name must be fully qualified, without a trailing dot.
** Returns 1 for a match, 0 for none.
*/
int ftpd_dsn_match(const char *pattern, const char *name)    asm("FTPLVMAT");

/*
** The catalog level that holds every name a pattern can match: what has to be
** read with LISTC LEVEL() before ftpd_dsn_match() filters the names.
**
** pattern -- the resolved data set name or pattern: uppercase, no quotes, no
**            trailing dot.  A name without '*' or '%' is the filter that
**            keeps the data set of that name and all data sets below it.
** level   -- receives the level, without a trailing dot.
**
** The level is the run of qualifiers in front of the first one that holds
** '*' or '%'.  A name without a wildcard has its last qualifier cut, because
** LISTC LEVEL(A.B) lists what is below A.B and not the data set A.B itself
** (measured on MVS 3.8j: IDC3012I ENTRY A.B. NOT FOUND), so the data set is
** found at the level above it.  A one-qualifier name is its own level.
**
**   SYS2.PROCLIB   -> SYS2           SYS2.**       -> SYS2
**   SYS2.PRO*.X    -> SYS2           SYS2          -> SYS2
**   SYS2.PROC%B    -> SYS2           SYS1.A.B.C    -> SYS1.A.B
**
** Returns:
**    1  level is filled in
**    0  there is no level: the first qualifier holds a wildcard ('**',
**       '*.PROCLIB'), the pattern holds a '(' or the level is not a plain
**       data set name (ftpd_dsn_valid()), or it does not fit in levelsz.
**       level is untouched; the caller keeps the level it had.  The level
**       ends up in LISTC LEVEL('...'), so only a checked name gets there.
*/
int ftpd_lvl_of(const char *pattern, char *level, int levelsz)   asm("FTPLVLOF");

#endif /* FTPD_LVL_H */
