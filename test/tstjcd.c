/* TSTJCD.C
** Tests for ftpd_jcd_is_job() -- which card of a submitted stream is the JOB
** statement that NOTIFY, USER and PASSWORD are injected behind (#277).
** Before it, any card containing " JOB " counted, a comment card included.
**
** ftpd#jcd.c is free of project and MVS headers, so this is a DUAL test.
*/
#include <stdio.h>
#include <string.h>

#include "ftpd#jcd.h"
#include <mbtcheck.h>

/* An 80-byte card: text padded with blanks, as build_card() makes it. */
static char card[81];

static int
is_job(const char *text)
{
    memset(card, ' ', 80);
    card[80] = '\0';
    memcpy(card, text, strlen(text));
    return ftpd_jcd_is_job(card, 80);
}

int
main(void)
{
    CHECK(is_job("//TESTJOB  JOB (ACCT),'NAME',CLASS=A") == 1, "typical");
    CHECK(is_job("//T JOB") == 1, "one char name, JOB is last");
    CHECK(is_job("//ABCDEFGH JOB") == 1, "eight char name");
    CHECK(is_job("//@#$ JOB") == 1, "national characters");
    CHECK(is_job("//J0B1 JOB") == 1, "digits in the name");
    CHECK(is_job("//JOB JOB") == 1, "name is JOB");
    CHECK(is_job("//X     JOB     ,CLASS=A") == 1, "many blanks");
    CHECK(is_job("//X JOB,CLASS=A") == 0, "comma right behind JOB");

    CHECK(is_job("//* RUN THE JOB NOW") == 0, "comment with JOB");
    CHECK(is_job("//*  X JOB") == 0, "comment looking like a JOB card");
    CHECK(is_job("//*X JOB") == 0, "comment, no blank after //*");
    CHECK(is_job("//* JOB") == 0, "comment, JOB after //*");
    CHECK(is_job("//") == 0, "null statement");
    CHECK(is_job("// JOB") == 0, "no name");
    CHECK(is_job("//  EXEC PGM=IEFBR14 JOB ") == 0, "JOB in the operands");
    CHECK(is_job("//STEP1 EXEC PGM=IEFBR14,PARM='A JOB B'") == 0,
          "JOB inside an operand");
    CHECK(is_job("//SYSIN DD * JOB ") == 0, "JOB behind DD");
    CHECK(is_job("//NAMETOOLONG JOB") == 0, "name of nine characters");
    CHECK(is_job("//A-B JOB") == 0, "bad character in the name");
    CHECK(is_job("//X JOBS") == 0, "operation JOBS");
    CHECK(is_job("//X JOBX") == 0, "operation JOBX");
    CHECK(is_job("//X XJOB") == 0, "operation XJOB");
    CHECK(is_job("//X MYJOB A") == 0, "MYJOB");
    CHECK(is_job("//X") == 0, "name only");
    CHECK(is_job("//X   ") == 0, "name and blanks only");

    CHECK(is_job("/* JOB") == 0, "delimiter");
    CHECK(is_job("/*") == 0, "delimiter only");
    CHECK(is_job("  //X JOB") == 0, "leading blanks");
    CHECK(is_job("X JOB") == 0, "no slashes");
    CHECK(is_job("/X JOB") == 0, "one slash");
    CHECK(is_job("RUN THE JOB NOW") == 0, "data with JOB");
    CHECK(is_job("") == 0, "blank card");
    CHECK(is_job("//x job") == 0, "lower case");

    CHECK(ftpd_jcd_is_job("//X JOB", 7) == 1, "JOB ends the buffer");
    CHECK(ftpd_jcd_is_job("//X JOB", 6) == 0, "buffer ends inside JOB");
    CHECK(ftpd_jcd_is_job("//X JOBS", 7) == 1, "len cuts the card at JOB");
    CHECK(ftpd_jcd_is_job("//X JOB", 2) == 0, "len 2");
    CHECK(ftpd_jcd_is_job("//X JOB", 0) == 0, "len 0");
    CHECK(ftpd_jcd_is_job("//X JOB", -1) == 0, "negative len");
    CHECK(ftpd_jcd_is_job(NULL, 80) == 0, "NULL");

    return mbt_test_summary("TSTJCD");
}
