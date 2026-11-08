/* TSTPRB.C
** Tests for ftpd_probe_accepted() -- how the start check (ftpd_auth_probe())
** reads the answer racf_login() gave to the check login.
**
** The check logs in with a userid and password that cannot be valid.  The
** cases pin what counts as an acceptance: an ACEE, a return code of 0, and
** the NULL with return code 8 that libc370 2.28.0 and later answers for a
** login whose ACEE is not the requested user's.  Every other refusal stays a
** refusal.
**
** ftpd#prb.c is free of project and MVS headers, so this is a DUAL test.
*/
#include <stdio.h>

#include "ftpd#prb.h"
#include <mbtcheck.h>

int
main(void)
{
    printf("=== FTPD start check tests ===\n");

    /* An ACEE is an acceptance, whatever the return code says. */
    CHECK(ftpd_probe_accepted(1, 0) == 1,
          "an ACEE with rc 0 is an accepted login");
    CHECK(ftpd_probe_accepted(1, FTPD_RACF_NOTAUTH) == 1,
          "an ACEE with rc 8 is an accepted login");
    CHECK(ftpd_probe_accepted(1, 28) == 1,
          "an ACEE with rc 28 is an accepted login");

    /* The security system said 0 and racf_login() returned no ACEE. */
    CHECK(ftpd_probe_accepted(0, 0) == 1,
          "rc 0 without an ACEE is an accepted login");

    /* libc370 2.28.0 and later: the ACEE was not the requested user's, so
    ** racf_login() deleted it and answered NULL with rc 8.  The security
    ** system accepted the check login all the same. */
    CHECK(ftpd_probe_accepted(0, FTPD_RACF_NOTAUTH) == 1,
          "NULL with rc 8 is an accepted login (libc370 2.28.0 and later)");

    /* Refusals: the security system verifies userids. */
    CHECK(ftpd_probe_accepted(0, 28) == 0,
          "NULL with rc 28 (RAKF: the userid is not known) is a refusal");
    CHECK(ftpd_probe_accepted(0, 4) == 0,
          "NULL with rc 4 (the user profile is not defined) is a refusal");
    CHECK(ftpd_probe_accepted(0, 12) == 0,
          "NULL with rc 12 (the password has expired) is a refusal");
    CHECK(ftpd_probe_accepted(0, 24) == 0,
          "NULL with rc 24 (failed by the installation exit) is a refusal");
    CHECK(ftpd_probe_accepted(0, 32) == 0,
          "NULL with rc 32 (the security system is not active) is a refusal");
    CHECK(ftpd_probe_accepted(0, -1) == 0,
          "NULL with a return code that was never stored is a refusal");

    return mbt_test_summary("TSTPRB");
}
