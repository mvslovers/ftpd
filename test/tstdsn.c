/* TSTDSN.C
** Tests for ftpd_dsn_valid() -- the character check every data set name now
** passes through before it reaches the catalog (issue #95).
**
** The regression that started it: `put build/ftpd.deploy.xmit` sends the
** local path as the remote name.  resolve_dsn() prepended the CWD prefix and
** uppercased, nothing looked at the '/', and the resulting
** HERC01.BUILD/FTPD.DEPLOY.XMIT was carried all the way to SVC 99 -- which
** answered "550 Cannot allocate dataset", blaming allocation for a name that
** was never a name.  The cases below pin down what a name may hold, and the
** two that must keep working alongside it: PDS members and LIST wildcards.
**
** ftpd#dsn.c is free of project and MVS headers, so this is a DUAL test: it
** runs natively via `make test-host` and on MVS via `make test-mvs`.  The
** EBCDIC cases below only mean something in the second run -- see the
** "letters are not a range" section.
*/
#include <stdio.h>

#include "ftpd#dsn.h"
#include <mbtcheck.h>

/* Valid as an ordinary name -- i.e. with wildcards not allowed. */
static int
ok(const char *dsn)
{
    return ftpd_dsn_valid(dsn, 0) == 1;
}

/* Rejected as an ordinary name. */
static int
bad(const char *dsn)
{
    return ftpd_dsn_valid(dsn, 0) == 0;
}

int
main(void)
{
    printf("=== FTPD data set name validation tests (#95) ===\n");

    /* --- the reported bug -------------------------------------------- */
    CHECK(bad("HERC01.BUILD/FTPD.DEPLOY.XMIT"),
          "a path in the name is rejected (was 550 Cannot allocate)");
    CHECK(bad("HERC01.BUILD/X"), "a '/' anywhere in the name is rejected");
    CHECK(bad("/U/HERC01/FILE"), "a UFS path is not a data set name");

    /* --- what a name may hold ----------------------------------------- */
    CHECK(ok("HERC01.FTPD.DEPLOY.XMIT"), "the name the client should send");
    CHECK(ok("SYS1.MACLIB"), "a two qualifier name");
    CHECK(ok("A"), "a single character name");
    CHECK(ok("HERC01.TEST1.V2R3"), "digits inside a qualifier");
    CHECK(ok("HERC01.@#$"), "the national characters @ # $");
    CHECK(ok("HERC01.MY-DATA"), "a hyphen");
    CHECK(ok("HERC01.GDG.G0001V00"), "a GDG generation data set");

    /* --- what a name may not hold ------------------------------------- */
    CHECK(bad("HERC01.MY DATA"), "a blank is rejected");
    CHECK(bad("HERC01.MY,DATA"), "a comma is rejected");
    CHECK(bad("'HERC01.TEST'"), "quotes are rejected -- resolve_dsn strips them");
    CHECK(bad("HERC01.TEST:1"), "a colon is rejected");
    CHECK(bad("HERC01.TEST+1"), "a plus sign is rejected");
    CHECK(bad("HERC01.TEST_1"), "an underscore is rejected");
    CHECK(bad("HERC01.TEST\t"), "a control character is rejected");

    /* --- a qualifier has to hold something ---------------------------- */
    CHECK(bad("HERC01..PROFILE"),
          "an empty qualifier is rejected (`put .profile` reached SVC 99)");
    CHECK(bad(".PROFILE"), "a leading dot is an empty first qualifier");
    CHECK(bad("HERC01."),
          "a trailing dot is an empty last qualifier -- resolve_dsn strips it "
          "first, where it means prefix-only");
    CHECK(bad("."), "a lone dot is not a name");
    CHECK(bad("HERC01...TEST"), "two empty qualifiers are rejected");

    /* --- letters are not a range -------------------------------------- */
    /* In EBCDIC A-Z runs C1-C9, D1-D9, E2-E9 with gaps, and '{' (C0),
    ** '}' (D0) and '\' (E0) sit inside what a range test would call a
    ** letter.  On MVS these three are the ones a `c >= 'A' && c <= 'Z'`
    ** check would wave through; here they must be rejected. */
    CHECK(bad("HERC01.TEST{"), "'{' is rejected (a letter to an EBCDIC range test)");
    CHECK(bad("HERC01.TEST}"), "'}' is rejected (a letter to an EBCDIC range test)");
    CHECK(bad("HERC01.TEST\\"), "'\\' is rejected (a letter to an EBCDIC range test)");

    /* --- the name arrives uppercase ----------------------------------- */
    CHECK(bad("herc01.test"), "lowercase is rejected -- the caller uppercases first");
    CHECK(bad("HERC01.Test"), "a single lowercase letter is rejected");

    /* --- a member is checked as well ----------------------------------- */
    /* The scan used to stop at the first '(' and never look at the rest, so
    ** whatever followed it -- in a CWD, a LIST argument, or the member of a
    ** DELE -- went on to the catalog and to IDCAMS unchecked (#335). */
    CHECK(ok("HERC01.PDS(MEM)"), "a member reference is valid");
    CHECK(ok("HERC01.PDS(MEM.EXT)"),
          "an extension is valid -- sanitize_member() cuts it off later");
    CHECK(ok("HERC01.PDS(@#$-1)"), "a member holds the characters a qualifier holds");
    CHECK(bad("HERC01.PDS(mem)"), "a lowercase member is rejected like a lowercase name");
    CHECK(bad("HERC01.P/DS(MEM)"), "the data set part of a member reference is checked");
    CHECK(bad("(MEM)"), "a member without a data set is not a name");
    CHECK(bad("HERC01.PDS(MEM"), "a member that is not closed is rejected");
    CHECK(bad("HERC01.PDS("), "an open parenthesis alone is rejected");
    CHECK(bad("HERC01.PDS()"), "an empty member is rejected");
    CHECK(bad("HERC01.PDS(.EXT)"), "a member with nothing before the extension is rejected");
    CHECK(bad("HERC01.PDS(MEM)X"), "text after the closing parenthesis is rejected");
    CHECK(bad("HERC01.PDS(MEM)(X)"), "a second member is rejected");
    CHECK(bad("HERC01.PDS(MEM))"), "a second closing parenthesis is rejected");
    CHECK(bad("HERC01.PDS(A(B)"), "an opening parenthesis inside the member is rejected");
    CHECK(bad("HERC01.PDS(M M)"), "a blank in a member is rejected");
    CHECK(bad("HERC01.PDS(M'M)"), "a quote in a member is rejected");
    CHECK(bad("HERC01.PDS(M,M)"), "a comma in a member is rejected");
    CHECK(bad("HERC01.PDS(M/M)"), "a '/' in a member is rejected");
    CHECK(bad("HERC01.PDS(MEM.E'T)"), "a quote in the extension is rejected");
    CHECK(bad("HERC01.PDS(M*M)"), "a pattern in a member is rejected when patterns are not allowed");
    CHECK(ftpd_dsn_valid("HERC01.PDS(M*M)", 1) == 1,
          "and accepted where patterns are allowed");
    CHECK(ftpd_dsn_valid("HERC01.PDS(M'M)", 1) == 0,
          "allowing patterns does not allow anything else in a member");
    CHECK(bad("HERC01.PDS(MEM)\x01"), "a control character after the member is rejected");

    /* --- wildcards belong to LIST and NLST ---------------------------- */
    /* '?' is one of them: FTPD has always documented and rejected it with
    ** '*' and '%' (FTPD_CONCEPT.md), and dsn_match() ignoring it means a
    ** pattern carrying one matches nothing -- not that it is a letter. */
    CHECK(bad("HERC01.MIK*"), "'*' is rejected when patterns are not allowed");
    CHECK(bad("HERC01.%BC"), "'%' is rejected when patterns are not allowed");
    CHECK(bad("HERC01.MIK?"), "'?' is rejected when patterns are not allowed");
    CHECK(ftpd_dsn_valid("HERC01.MIK*", 1) == 1, "'*' is a pattern for LIST");
    CHECK(ftpd_dsn_valid("HERC01.%BC", 1) == 1, "'%' is a pattern for LIST");
    CHECK(ftpd_dsn_valid("HERC01.MIK?", 1) == 1, "'?' is a pattern for LIST");
    CHECK(ftpd_dsn_valid("HERC01.*", 1) == 1, "a whole qualifier may be a pattern");
    CHECK(ftpd_dsn_valid("HERC01.BUILD/X", 1) == 0,
          "allowing patterns does not allow anything else");
    CHECK(ftpd_dsn_valid("HERC01..*", 1) == 0,
          "allowing patterns does not allow an empty qualifier");

    /* --- nothing is not a name ---------------------------------------- */
    CHECK(bad(""), "an empty name is rejected");
    CHECK(ftpd_dsn_valid(NULL, 0) == 0, "a NULL name is rejected");
    CHECK(ftpd_dsn_valid(NULL, 1) == 0, "a NULL name is rejected for LIST too");

    return mbt_test_summary("TSTDSN");
}
