/* TSTANM.C
** Tests for ftpd_acee_user_matches() -- the check that the ACEE racf_login()
** returned belongs to the user who logged in.
**
** A login has to return the ACEE of the requested user.  The cases pin the
** comparison: same name matches however it is cased or
** padded, a different name does not, and nothing short of the whole name
** counts.
**
** ftpd#anm.c is free of project and MVS headers, so this is a DUAL test.
*/
#include <stdio.h>
#include <stddef.h>
#include <string.h>

#include "ftpd#anm.h"
#include <mbtcheck.h>

/* Build the ACEEUSER field: length byte, then the name padded to 8. */
static void
aceeuser(char *out, const char *name)
{
    size_t len = strlen(name);

    memset(out, ' ', 9);
    out[0] = (char)len;
    memcpy(out + 1, name, len);
}

int
main(void)
{
    char field[9];

    printf("=== FTPD ACEE user name check tests ===\n");

    aceeuser(field, "ALICE");
    CHECK(ftpd_acee_user_matches(field, "ALICE") == 1,
          "same name matches");
    CHECK(ftpd_acee_user_matches(field, "alice") == 1,
          "the requested name is compared in upper case");
    CHECK(ftpd_acee_user_matches(field, "Alice") == 1,
          "mixed case matches");
    CHECK(ftpd_acee_user_matches(field, "BOB") == 0,
          "a different name does not match");
    CHECK(ftpd_acee_user_matches(field, "ALICE2") == 0,
          "a longer requested name does not match");
    CHECK(ftpd_acee_user_matches(field, "ALIC") == 0,
          "a shorter requested name does not match");
    CHECK(ftpd_acee_user_matches(field, "") == 0,
          "an empty requested name does not match");
    CHECK(ftpd_acee_user_matches(field, "ALICE   ") == 1,
          "blanks after the requested name do not count");

    /* The ACEE of the started task is not the ACEE of a client. */
    aceeuser(field, "STC");
    CHECK(ftpd_acee_user_matches(field, "ALICE") == 0,
          "the started task's ACEE is not the requested user's");
    CHECK(ftpd_acee_user_matches(field, "NOSUCH") == 0,
          "the started task's ACEE is not an unknown user's");
    CHECK(ftpd_acee_user_matches(field, "STC") == 1,
          "a login as the started task's own name does match by name");

    /* The full 8 characters, and the cut at 8. */
    aceeuser(field, "ABCDEFGH");
    CHECK(ftpd_acee_user_matches(field, "ABCDEFGH") == 1,
          "an 8 character name matches");
    CHECK(ftpd_acee_user_matches(field, "ABCDEFGHI") == 1,
          "a requested name longer than 8 is cut to 8, as racf_login() does");
    CHECK(ftpd_acee_user_matches(field, "ABCDEFGX") == 0,
          "a difference in the last character is seen");

    /* A length byte that says more than the field holds is clamped. */
    memset(field, ' ', 9);
    field[0] = (char)200;
    memcpy(field + 1, "ABCDEFGH", 8);
    CHECK(ftpd_acee_user_matches(field, "ABCDEFGH") == 1,
          "a length byte above 8 is read as 8");

    /* An ACEE with no name is no user. */
    memset(field, ' ', 9);
    field[0] = 0;
    CHECK(ftpd_acee_user_matches(field, "ALICE") == 0,
          "an ACEE with an empty name matches nobody");
    CHECK(ftpd_acee_user_matches(field, "") == 0,
          "an empty name matches nothing, not even an empty one");

    memset(field, ' ', 9);
    field[0] = 8;
    CHECK(ftpd_acee_user_matches(field, "ALICE") == 0,
          "a blank name with a length of 8 matches nobody");

    {
        char name[9];

        aceeuser(field, "ALICE");
        ftpd_acee_user_name(name, field);
        CHECK(strcmp(name, "ALICE") == 0, "the name is returned unpadded");

        aceeuser(field, "ABCDEFGH");
        ftpd_acee_user_name(name, field);
        CHECK(strcmp(name, "ABCDEFGH") == 0, "an 8 character name is whole");

        memset(field, ' ', 9);
        field[0] = 0;
        ftpd_acee_user_name(name, field);
        CHECK(name[0] == '\0', "an empty name is an empty string");

        ftpd_acee_user_name(name, NULL);
        CHECK(name[0] == '\0', "no field is an empty string");
    }

    memset(field, ' ', 9);
    field[0] = 8;
    CHECK(ftpd_acee_user_matches(NULL, "ALICE") == 0,
          "no ACEE field matches nobody");
    CHECK(ftpd_acee_user_matches(field, NULL) == 0,
          "no requested name matches nothing");

    return mbt_test_summary("TSTANM");
}
