/*
** TSTPRM -- ftpd#119: a read error on DD:FTPDPRM must abandon the start,
**           not come up on half a configuration.
**
** Since libc370 1.0.4 an uncorrectable I/O error is ferror() + errno EIO
** instead of ABEND S001, and feof() is deliberately NOT set.  That turns
** ftpdcfg_load()'s
**
**     while (fgets(line, sizeof(line), fp)) parse_line(cfg, line);
**
** into a silent truncation: whatever was parsed before the bad track stays,
** everything after it keeps its ftpdcfg_defaults() value, and the function
** used to answer 0 -- so FTPD started, looked healthy, and was missing DASD
** volumes and SITE/JES settings nobody asked it to drop.
**
** The error vector is libc370's own, from its #147 item 3 probe: write a
** normal FB/3120 data set, then read it back through a DD whose DCB override
** claims BLKSIZE=80.  The first READ meets a 3120-byte block with an 80-byte
** buffer -- a wrong-length record, an uncorrectable I/O error.  Both DDs are
** allocated here through __dsalcf(DDNAME=...), so the vector needs nothing
** from the JCL and the test carries its own fixture.
**
** Two rounds, and the first one matters as much as the second:
**
**   round 1  a HEALTHY FTPDPRM parses and returns 0, with the values from
**            the member actually in the config.  Without this the second
**            round proves nothing -- a ftpdcfg_load() that failed for any
**            reason at all would pass it.
**   round 2  the SAME member behind the lying BLKSIZE returns non-zero.
**
** MVS-only: the vector is a real BSAM condition, and DD: names, __dsalcf()
** and WTO have no host equivalent.
**
** PARM='<dsn>' names the scratch data set (default IBMUSER.FTPD.TPRM);
** it is created and deleted by this test.
**
** RC 0 = all checks passed (ftpd.c turns ftpdcfg_load()'s non-zero into 4).
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <clibio.h>
#include <clibwto.h>
#include <mbtcheck.h>

#include "ftpd#cfg.h"

#define DFLT_DSN        "IBMUSER.FTPD.TPRM"

/* Written into the scratch data set, and asserted back in round 1.
** Deliberately not what ftpdcfg_defaults() would leave behind, so "parsed"
** and "did not parse" cannot be confused. */
#define TSTPRM_PORT     2121
#define TSTPRM_MAXSESS  17

static const char * const parmlines[] = {
    "# TSTPRM -- ftpd#119 parmlib read-error probe",
    "SRVPORT=2121",
    "MAXSESSIONS=17",
    NULL
};

/* --------------------------------------------------------------------
** Write the parmlib member through a correctly described DD.
** Returns 0 on success.
** ----------------------------------------------------------------- */
static int
write_parmlib(const char *dsn)
{
    char  dd[9];
    char  ddspec[16];
    FILE *fp;
    int   i;
    int   rc;

    rc = __dsalcf(dd, "DDNAME=TSTPRMW;DSN=%s;DISP=(NEW,CATLG,DELETE);"
                      "DSORG=PS;RECFM=FB;LRECL=80;BLKSIZE=3120;"
                      "SPACE=TRK(1,1)", dsn);
    if (rc != 0) {
        printf("  __dsalcf(TSTPRMW) failed rc=%d\n", rc);
        return -1;
    }

    snprintf(ddspec, sizeof(ddspec), "dd:%s", dd);

    fp = fopen(ddspec, "w");
    if (!fp) {
        printf("  fopen(%s) failed errno=%d\n", ddspec, errno);
        __dsfree(dd);
        return -1;
    }

    for (i = 0; parmlines[i]; i++)
        fprintf(fp, "%s\n", parmlines[i]);

    fclose(fp);
    __dsfree(dd);

    return 0;
}

/* --------------------------------------------------------------------
** Allocate DD:FTPDPRM over the data set written above.  blksize 0 means
** "tell the truth" (take the DSCB); anything else is the lying override
** that makes the first READ a wrong-length record.
** ----------------------------------------------------------------- */
static int
alloc_ftpdprm(const char *dsn, int blksize)
{
    char dd[9];
    int  rc;

    if (blksize == 0)
        rc = __dsalcf(dd, "DDNAME=FTPDPRM;DSN=%s;DISP=SHR", dsn);
    else
        rc = __dsalcf(dd, "DDNAME=FTPDPRM;DSN=%s;DISP=SHR;"
                          "RECFM=FB;LRECL=80;BLKSIZE=%d", dsn, blksize);

    if (rc != 0)
        printf("  __dsalcf(FTPDPRM,blksize=%d) failed rc=%d\n", blksize, rc);

    return rc;
}

int
main(int argc, char **argv)
{
    const char   *dsn = DFLT_DSN;
    ftpd_config_t cfg;
    char          dd[9];
    int           rc;

    if (argc > 1 && argv[1] && argv[1][0])
        dsn = argv[1];

    printf("TSTPRM -- ftpd#119: DD:FTPDPRM read error must abandon "
           "the start (dsn '%s')\n\n", dsn);

    if (write_parmlib(dsn) != 0) {
        /* No vector, no verdict -- say so loudly rather than pass on a
        ** test that never ran.  wtof() as well as printf(), the #145
        ** lesson: a stdio failure can take buffered SYSPRINT with it. */
        wtof("TSTPRM: CANNOT BUILD THE FIXTURE DATA SET -- NO VERDICT");
        printf("FAIL: fixture data set could not be written\n");
        return 8;
    }

    /* ---- round 1: a healthy parmlib is read, and parsed ------------ */

    if (alloc_ftpdprm(dsn, 0) == 0) {
        memset(&cfg, 0, sizeof(cfg));
        rc = ftpdcfg_load(&cfg);

        CHECK_EQ(rc, 0, "healthy FTPDPRM: ftpdcfg_load() returns 0");
        CHECK_EQ(cfg.port, TSTPRM_PORT, "healthy FTPDPRM: SRVPORT parsed");
        CHECK_EQ(cfg.max_sessions, TSTPRM_MAXSESS,
                 "healthy FTPDPRM: MAXSESSIONS parsed");

        __dsfree("FTPDPRM");
    } else {
        CHECK(0, "healthy FTPDPRM: allocated");
    }

    /* ---- round 2: the same member behind a lying BLKSIZE ----------- */

    if (alloc_ftpdprm(dsn, 80) == 0) {
        memset(&cfg, 0, sizeof(cfg));
        errno = 0;
        rc = ftpdcfg_load(&cfg);

        /* The point of the whole test.  Before the guard this was 0 and
        ** FTPD started on whatever had been parsed. */
        CHECK(rc != 0,
              "read error on FTPDPRM: ftpdcfg_load() returns non-zero");

        /* The measurement behind the check above.  Round 1 read the same
        ** member successfully, so the two values here say how far round 2
        ** got before the error: SRVPORT is the first keyword and comes back
        ** parsed, MAXSESSIONS is the second and comes back at its
        ** ftpdcfg_defaults() value.  That is the truncated parse the guard
        ** exists to refuse -- without it this line read
        ** "rc=0 errno=5 port=2121 maxsess=10" and FTPD started on it. */
        printf("  (round 2: rc=%d errno=%d port=%d maxsess=%d)\n",
               rc, errno, cfg.port, cfg.max_sessions);

        __dsfree("FTPDPRM");
    } else {
        CHECK(0, "read error on FTPDPRM: allocated");
    }

    /* Scratch data set: gone either way, so a re-run finds a clean slate
    ** rather than a NEW allocation that fails against the leftover. */
    if (__dsalcf(dd, "DSN=%s;DISP=(OLD,DELETE)", dsn) == 0)
        __dsfree(dd);

    return mbt_test_summary("TSTPRM");
}
