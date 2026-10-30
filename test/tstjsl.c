/* TSTJSL.C
** Tests for the spool file table of LIST JOBnnnnn in the JES mode (#370).
** FTPD answered with its own table that had no ID column and no line of
** dashes; clients that read the z/OS form (the Zowe FTP plug-in finds the
** table by "ID" at the start of a line, the columns by the position of the
** header words, the end by "N spool files") reported "No spool file." for a
** job that had four.
**
** ftpd#jsl.c is free of project and MVS headers, so this is a DUAL test.
*/
#include <stdio.h>
#include <string.h>

#include "ftpd#jsl.h"
#include <mbtcheck.h>

/* The offset of the first non-blank character at or after from, or -1. */
static int
word_at(const char *s, int from)
{
    int i;

    for (i = from; s[i]; i++) {
        if (s[i] != ' ')
            return i;
    }
    return -1;
}

int
main(void)
{
    char head[96];
    char row[96];
    char line[96];
    int n;
    int stepcol;
    int proccol;
    int classcol;
    int ddcol;
    int bytecol;

    /* The fixed lines, byte for byte */
    n = ftpd_jsl_rule(line, sizeof(line));
    CHECK(n == 8 && strcmp(line, "--------") == 0, "rule");

    n = ftpd_jsl_head(head, sizeof(head));
    CHECK(strcmp(head,
        "        ID  STEPNAME PROCSTEP C DDNAME   BYTE-COUNT ") == 0,
        "header text");
    CHECK(n == (int)strlen(head), "header length is returned");
    CHECK(head[0] == ' ' && word_at(head, 0) > 0,
          "header starts with blanks, then ID (the client's anchor)");
    CHECK(strncmp(head + word_at(head, 0), "ID", 2) == 0, "ID comes first");
    CHECK(head[n - 1] == ' ', "one blank after BYTE-COUNT");

    /* A JES2 file: the step is JES2 though the spool holds none */
    n = ftpd_jsl_row(row, sizeof(row), 1, "", "", 'H', "JESMSGLG", 1064L, 1);
    CHECK(strcmp(row,
        "        001 JES2              H JESMSGLG       1064") == 0,
        "JES2 file: ID 001, step JES2, class H, 1064 bytes");
    CHECK(n == (int)strlen(row), "row length is returned");

    /* A user file: step and proc step from the spool, no JES2 default */
    n = ftpd_jsl_row(row, sizeof(row), 5, "STEP1", "PROC1", 'A', "SYSPRINT",
                     484L, 0);
    CHECK(strcmp(row,
        "        005 STEP1    PROC1    A SYSPRINT        484") == 0,
        "user file with a proc step");

    n = ftpd_jsl_row(row, sizeof(row), 4, "", "", 'H', "SYSUT2", 80L, 0);
    CHECK(strncmp(row + 12, "         ", 9) == 0,
          "no step on a file of the user: blank, not JES2");

    /* The columns of every row sit under the words of the header */
    ftpd_jsl_head(head, sizeof(head));
    stepcol = word_at(head, 8 + 2);
    proccol = word_at(head, stepcol + 8);
    classcol = word_at(head, proccol + 8);
    ddcol = word_at(head, classcol + 1);
    bytecol = word_at(head, ddcol + 6);
    CHECK(strncmp(head + stepcol, "STEPNAME", 8) == 0, "STEPNAME column");
    CHECK(strncmp(head + proccol, "PROCSTEP", 8) == 0, "PROCSTEP column");
    CHECK(head[classcol] == 'C', "C column");
    CHECK(strncmp(head + ddcol, "DDNAME", 6) == 0, "DDNAME column");
    CHECK(strncmp(head + bytecol, "BYTE-COUNT", 10) == 0, "BYTE-COUNT column");

    ftpd_jsl_row(row, sizeof(row), 12, "STEP2", "PROC2", 'X', "SYSOUT",
                 123456L, 0);
    CHECK(strncmp(row + 8, "012", 3) == 0 && row[11] == ' ',
          "ID is three digits at the position of ID");
    CHECK(strncmp(row + stepcol, "STEP2   ", 8) == 0 &&
          row[stepcol + 8] == ' ', "step under STEPNAME");
    CHECK(strncmp(row + proccol, "PROC2   ", 8) == 0 &&
          row[proccol + 8] == ' ', "proc step under PROCSTEP");
    CHECK(row[classcol] == 'X' && row[classcol - 1] == ' ' &&
          row[classcol + 1] == ' ', "class under C");
    CHECK(strncmp(row + ddcol, "SYSOUT  ", 8) == 0 && row[ddcol + 8] == ' ',
          "ddname under DDNAME");
    CHECK(strcmp(row + bytecol, "    123456") == 0,
          "byte count is right-aligned and ends at the end of BYTE-COUNT");
    CHECK((int)strlen(row) == bytecol + 10, "the row ends with the count");

    /* Edges */
    ftpd_jsl_row(row, sizeof(row), 3, "STEP1", "", 0, "SYSUT1", 0L, 0);
    CHECK(row[classcol] == ' ', "no class prints a blank");
    CHECK(strcmp(row + bytecol, "         0") == 0, "empty file: 0 bytes");

    ftpd_jsl_row(row, sizeof(row), 3, "VERYLONGSTEP", "LONGPROCSTEP", 'A',
                 "VERYLONGDD", 1L, 0);
    CHECK(strncmp(row + stepcol, "VERYLONG ", 9) == 0 &&
          strncmp(row + proccol, "LONGPROC ", 9) == 0 &&
          strncmp(row + ddcol, "VERYLONG ", 9) == 0,
          "names longer than 8 are cut, the columns stay put");

    ftpd_jsl_row(row, sizeof(row), 3, "S", "", 'A', "D", 2147483647L, 0);
    CHECK(strcmp(row + bytecol, "2147483647") == 0,
          "the largest count fills the 10 digits, the row keeps its width");
    ftpd_jsl_row(row, sizeof(row), 3, "S", "", 'A', "D", -5L, 0);
    CHECK(strcmp(row + bytecol, "         0") == 0, "a negative count is 0");

    ftpd_jsl_row(row, sizeof(row), 999, "S", "", 'A', "D", 1L, 0);
    CHECK(strncmp(row + 8, "999", 3) == 0, "ID 999 is the last");
    CHECK(ftpd_jsl_row(row, sizeof(row), 1000, "S", "", 'A', "D", 1L, 0) == -1,
          "ID 1000 does not fit three digits");
    CHECK(ftpd_jsl_row(row, sizeof(row), 0, "S", "", 'A', "D", 1L, 0) == -1,
          "ID 0 does not exist: the numbers start at 1");

    ftpd_jsl_row(row, sizeof(row), 1, NULL, NULL, 'A', NULL, 1L, 0);
    CHECK(strncmp(row + 8, "001", 3) == 0, "NULL names are empty names");

    CHECK(ftpd_jsl_row(row, 20, 1, "S", "", 'A', "D", 1L, 0) == -1,
          "a row that does not fit the buffer is -1");
    CHECK(ftpd_jsl_head(row, 10) == -1, "a header that does not fit is -1");
    CHECK(ftpd_jsl_rule(row, 8) == -1, "no room for the NUL is -1");

    /* The last line: "spool files" in the plural for every count */
    n = ftpd_jsl_tail(line, sizeof(line), 3);
    CHECK(n == 13 && strcmp(line, "3 spool files") == 0, "tail for 3");
    ftpd_jsl_tail(line, sizeof(line), 1);
    CHECK(strcmp(line, "1 spool files") == 0, "tail for 1 stays plural");
    ftpd_jsl_tail(line, sizeof(line), 12);
    CHECK(strcmp(line, "12 spool files") == 0, "tail for 12");
    CHECK(ftpd_jsl_tail(line, 5, 3) == -1, "a tail that does not fit is -1");

    return mbt_test_summary("TSTJSL");
}
