/* TSTLVL.C
** Tests for ftpd_lvl_of() and ftpd_dsn_match() -- which catalog level LIST and
** NLST read for a data set name or pattern, and which of the names found it
** keeps (issue #331).
**
** The report: `zowe zftp ls ds "SYS2.PROCLIB"` and `ls ds "SYS2.**"` came
** back empty from a working directory that was not SYS2, because the catalog
** was read at the level of the working directory and only filtered there.
**
** Free of project and MVS headers, so a DUAL test: native via `mbt test`,
** on MVS via `mbt test --mvs`.
*/
#include <stdio.h>
#include <string.h>

#include "ftpd#lvl.h"
#include <mbtcheck.h>

static char lvl[64];

/* The level of pattern; the buffer is poisoned first so that "untouched"
** can be told from "written empty". */
static int
level_of(const char *pattern)
{
    memset(lvl, '#', sizeof(lvl) - 1);
    lvl[sizeof(lvl) - 1] = '\0';
    return ftpd_lvl_of(pattern, lvl, sizeof(lvl));
}

static int
level_is(const char *pattern, const char *want)
{
    return level_of(pattern) == 1 && strcmp(lvl, want) == 0;
}

static int
no_level(const char *pattern)
{
    return level_of(pattern) == 0 && lvl[0] == '#';
}

int
main(void)
{
    printf("=== FTPD LIST catalog level tests (#331) ===\n");

    /* --- the two reported names --------------------------------------- */
    CHECK(level_is("SYS2.PROCLIB", "SYS2"),
          "a full name is found at the level above it: LEVEL(A.B) does not "
          "list A.B");
    CHECK(level_is("SYS2.**", "SYS2"),
          "SYS2.** reads SYS2, '**' is no part of a LEVEL()");

    /* --- the level is the literal run in front of the first pattern --- */
    CHECK(level_is("SYS2.PRO*", "SYS2"), "a '*' inside a qualifier");
    CHECK(level_is("SYS2.PROC%B", "SYS2"), "a '%' inside a qualifier");
    CHECK(level_is("SYS2.*", "SYS2"), "a '*' qualifier");
    CHECK(level_is("SYS2.PRO*.X", "SYS2"),
          "qualifiers after the pattern are not part of the level");
    CHECK(level_is("SYS1.A.B.*", "SYS1.A.B"), "several literal qualifiers");
    CHECK(level_is("SYS1.A.B**", "SYS1.A"), "'**' inside the last qualifier");
    CHECK(level_is("A.B.%.D", "A.B"), "a '%' qualifier in the middle");

    /* --- a name without a wildcard: cut the last qualifier ------------ */
    CHECK(level_is("SYS1.A.B.C", "SYS1.A.B"), "a long full name");
    CHECK(level_is("SYS2.PROCLIB.OLD", "SYS2.PROCLIB"),
          "the children of a data set are read at the data set");
    CHECK(level_is("SYS2", "SYS2"), "one qualifier is its own level");

    /* --- no level: the first qualifier is a pattern ------------------- */
    CHECK(no_level("**"), "** has no level");
    CHECK(no_level("*"), "* has no level");
    CHECK(no_level("*.PROCLIB"), "*.PROCLIB has no level");
    CHECK(no_level("SY%.PROCLIB"), "SY%.PROCLIB has no level");
    CHECK(no_level("SYS*"), "SYS* has no level");
    CHECK(no_level(""), "an empty name has no level");
    CHECK(no_level(NULL), "no name has no level");

    /* --- no level: what LISTC LEVEL('%s') must never be given ---------- */
    CHECK(no_level("A.B(x"), "an open '(' is no level");
    CHECK(no_level("A.B(x).C"), "a member in front of a qualifier is no level");
    CHECK(no_level("A.B(x).*"), "a member in front of a wildcard qualifier");
    CHECK(no_level("A.(x.*"), "a '(' in a literal qualifier of a pattern");
    CHECK(no_level("A.B'C.D"), "a quote would end the LEVEL() early");
    CHECK(no_level("A.B C.D"), "a blank is no data set name character");
    CHECK(no_level("A..B"), "an empty qualifier is no level");
    CHECK(no_level("A.B?.C"), "a '?' is not a character of a level");
    CHECK(no_level("A.B(x).C*"), "a '(' stays out of the level");
    CHECK(level_is("A#.B@.$C-1.*", "A#.B@.$C-1"),
          "the national characters and the hyphen are allowed");

    /* --- a level that does not fit ------------------------------------ */
    CHECK(ftpd_lvl_of("SYS2.PROCLIB", lvl, 5) == 1 && strcmp(lvl, "SYS2") == 0,
          "a level of exactly levelsz-1 characters fits");
    CHECK(level_of("SYS2.PROCLIB") == 1 &&
          ftpd_lvl_of("SYS2.PROCLIB", lvl, 4) == 0,
          "a level that leaves no room for the NUL is not written");
    CHECK(ftpd_lvl_of("SYS2.PROCLIB", lvl, 0) == 0, "a zero buffer is safe");

    /* --- the matcher that filters what the level returned ------------- */
    CHECK(ftpd_dsn_match("SYS2.**", "SYS2.PROCLIB") == 1,
          "SYS2.** matches SYS2.PROCLIB");
    CHECK(ftpd_dsn_match("SYS2.**", "SYS2.A.B.C.D") == 1,
          "SYS2.** matches any number of further qualifiers");
    CHECK(ftpd_dsn_match("SYS2.**", "SYS1.PROCLIB") == 0,
          "SYS2.** does not match SYS1");
    CHECK(ftpd_dsn_match("SYS2.*", "SYS2.PROCLIB") == 1,
          "SYS2.* matches one qualifier");
    CHECK(ftpd_dsn_match("SYS2.*", "SYS2.A.B") == 0,
          "SYS2.* does not match two");
    CHECK(ftpd_dsn_match("SYS2.PRO*", "SYS2.PROCLIB") == 1,
          "a '*' ends a qualifier");
    CHECK(ftpd_dsn_match("SYS2.PRO*", "SYS2.PROCLIB.OLD") == 0,
          "a '*' stays inside its qualifier");
    CHECK(ftpd_dsn_match("SYS2.PROC%IB", "SYS2.PROCLIB") == 1 &&
          ftpd_dsn_match("SYS2.PROC%IB", "SYS2.PROCIB") == 0 &&
          ftpd_dsn_match("SYS2.PROC%IB", "SYS2.PROCLLIB") == 0,
          "a '%' is exactly one character");
    CHECK(ftpd_dsn_match("sys2.proclib", "SYS2.PROCLIB") == 1,
          "literals match without regard to case");
    CHECK(ftpd_dsn_match("A.**.Z", "A.B.C.Z") == 1 &&
          ftpd_dsn_match("A.**.Z", "A.Z") == 1 &&
          ftpd_dsn_match("A.**.Z", "A.B.C") == 0,
          "** between literals takes any number of qualifiers, also none");

    /* --- consecutive '**' are one '**' -------------------------------- */
    CHECK(ftpd_dsn_match("**.**.**.**.**.**.**.**.**.**.**.**.**.**.X",
                         "A.B.C.D.E.F.G.H.I.J.K.L.M.N.O.P.Q.R.S.T.Y") == 0,
          "14 '**' in a row take no time to fail");
    CHECK(ftpd_dsn_match("A.**.**.Z", "A.B.C.Z") == 1 &&
          ftpd_dsn_match("A.**.**.Z", "A.Z") == 1,
          "A.**.**.Z is A.**.Z");

    return mbt_test_summary("TSTLVL");
}
