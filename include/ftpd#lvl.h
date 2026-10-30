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
**
** The run time is bounded: '**' between literals tries the rest of the
** pattern at every qualifier boundary, which for a crafted pattern and a name
** of many short qualifiers is millions of calls.  After
** FTPD_DSN_MATCH_LIMIT calls the match gives up and answers 0.  A pattern
** that matches needed under 200 calls in a fuzz of 3 million, so a real
** lookup never gets there.
*/
#define FTPD_DSN_MATCH_LIMIT 10000

int ftpd_dsn_match(const char *pattern, const char *name)    asm("FTPLVMAT");

/*
** The same with the limit given and the number of calls reported, for the
** test.  used may be NULL.
*/
int ftpd_dsn_match_limit(const char *pattern, const char *name, int limit,
                         int *used)                          asm("FTPLVMLM");

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

/*
** Does LIST/NLST show this data set of the catalog level it read?
**
** The one decision both passes over the level share -- the count that tells
** an empty result from a listing, and the listing itself.  An empty result is
** answered 550 before the data connection is opened, since a 226 behind an
** empty list leaves a client that expects a line (the Zowe FTP plugin)
** waiting for it (#344).
**
** filter       -- the resolved name or pattern the client asked for, or
**                 NULL when it asked for the whole level.
** has_wildcard -- nonzero when filter holds '*' or '%'.
** dsn          -- a data set name of the level.
**
** Returns 1 when dsn is shown: no filter; a pattern that ftpd_dsn_match()
** accepts; or, for a name without a wildcard, the data set of that name and
** every data set below it.
*/
int ftpd_lvl_keep(const char *filter, int has_wildcard, const char *dsn)
                                                            asm("FTPLVKEP");

/*
** The index of the first name of a catalog level that LIST/NLST shows, or -1
** when it shows none.
**
** The count that tells an empty result from a listing only has to know
** whether there is a first hit, so it stops there; a full count would match
** every entry twice per command, once to count and once to print (#348).
**
** name_at -- answers the name at index i, or NULL at the end of the level.
**            It is not called again after it has answered NULL, nor after
**            the first name that is kept.
** ctx     -- handed to name_at as it is.
** filter and has_wildcard are those of ftpd_lvl_keep().
*/
int ftpd_lvl_first(const char *filter, int has_wildcard,
                   const char *(*name_at)(void *ctx, int i), void *ctx)
                                                            asm("FTPLVFST");

#endif /* FTPD_LVL_H */
