/* TSTJCD.C
** Tests for ftpd_jcd_is_job() -- which card of a submitted stream is the JOB
** statement that NOTIFY, USER and PASSWORD are injected behind (#277).
** Before it, any card containing " JOB " counted, a comment card included.
**
** And for ftpd_jcd_feed() -- where that statement ends, so the credentials
** never land behind a card that is not part of it (#383).
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

/* Feed one card to the tracker; card holds the (possibly changed) result. */
static ftpd_jcd_t st;

static int
feed(const char *text)
{
    memset(card, ' ', 80);
    card[80] = '\0';
    memcpy(card, text, strlen(text));
    return ftpd_jcd_feed(&st, card, 80);
}

/* Does the card the last feed() left equal text, padded with blanks? */
static int
card_is(const char *text)
{
    char want[81];

    memset(want, ' ', 80);
    want[80] = '\0';
    memcpy(want, text, strlen(text));
    return memcmp(card, want, 80) == 0;
}

/* A card of n characters c at index from. */
static void
fill(char *buf, int from, int n, char c)
{
    int i;

    for (i = 0; i < n; i++)
        buf[from + i] = c;
}

static void
test_statement_end(void)
{
    char buf[81];

    /* the shapes that worked before */
    ftpd_jcd_init(&st);
    CHECK(feed("//TESTJOB JOB (ACCT),'NAME',CLASS=A") == FTPD_JCD_DONE,
          "plain card ends the statement");
    CHECK(card_is("//TESTJOB JOB (ACCT),'NAME',CLASS=A,"), "comma appended");
    CHECK(st.nheld == 1 && st.phase == 2, "one card held, statement done");
    CHECK(feed("//STEP1 EXEC PGM=IEFBR14") == FTPD_JCD_PASS,
          "the body is not touched");
    CHECK(card_is("//STEP1 EXEC PGM=IEFBR14"), "body card unchanged");
    CHECK(feed("//SECOND JOB (ACCT)") == FTPD_JCD_PASS &&
          card_is("//SECOND JOB (ACCT)"),
          "a second JOB card is body (#270), not changed");

    ftpd_jcd_init(&st);
    CHECK(feed("//J JOB (ACCT),'NAME',") == FTPD_JCD_HOLD,
          "trailing comma continues");
    CHECK(card_is("//J JOB (ACCT),'NAME',"), "held card unchanged");
    CHECK(ftpd_jcd_unfinished(&st) == 1, "unfinished inside the statement");
    CHECK(feed("//  CLASS=A") == FTPD_JCD_DONE, "last continuation line");
    CHECK(card_is("//  CLASS=A,"), "comma on the last line");
    CHECK(st.nheld == 2 && ftpd_jcd_unfinished(&st) == 0, "two cards held");

    ftpd_jcd_init(&st);
    CHECK(feed("//* a comment before") == FTPD_JCD_PASS,
          "comment card before the JOB card is put at once");
    CHECK(ftpd_jcd_unfinished(&st) == 0, "not inside the statement yet");
    CHECK(feed("//J JOB (A)") == FTPD_JCD_DONE && card_is("//J JOB (A),"),
          "JOB card behind a comment card");

    ftpd_jcd_init(&st);
    CHECK(feed("//J JOB (A),") == FTPD_JCD_HOLD, "first line");
    CHECK(feed("//* between the lines") == FTPD_JCD_HOLD,
          "comment card between continuation lines is no end");
    CHECK(feed("//* and another,") == FTPD_JCD_HOLD, "second comment card");
    CHECK(feed("//  CLASS=A") == FTPD_JCD_DONE, "statement ends behind them");
    CHECK(st.nheld == 4, "comment cards are held with the statement");

    /* a comma in the comment field is not a continuation (#383) */
    ftpd_jcd_init(&st);
    CHECK(feed("//J JOB (A),'N',CLASS=A   NOTE, MORE") == FTPD_JCD_DONE,
          "comma in the comment field");
    CHECK(card_is("//J JOB (A),'N',CLASS=A,  NOTE, MORE"),
          "comma goes behind the operands, comment untouched");

    ftpd_jcd_init(&st);
    CHECK(feed("//J JOB A COMMENT,") == FTPD_JCD_DONE, "single blank");
    CHECK(card_is("//J JOB A, COMMENT,"), "comment moved, not glued");

    ftpd_jcd_init(&st);
    memset(buf, ' ', 80); buf[80] = '\0';
    memcpy(buf, "//J JOB A ", 10);
    fill(buf, 10, 60, 'X');         /* comment up to column 70 */
    CHECK(feed(buf) == FTPD_JCD_DONE, "comment fits when moved");
    CHECK(card[9] == ',' && card[10] == ' ' && card[11] == 'X' &&
          card[70] == 'X' && card[71] == ' ',
          "comment moved by one, inside column 71");

    ftpd_jcd_init(&st);
    memset(buf, ' ', 80); buf[80] = '\0';
    memcpy(buf, "//J JOB A ", 10);
    fill(buf, 10, 61, 'X');         /* comment up to column 71 */
    CHECK(feed(buf) == FTPD_JCD_DONE, "comment too long to move");
    CHECK(card[9] == ',' && card[10] == ' ' && card[70] == ' ' &&
          card[71] == ' ', "comment dropped, never into column 72");

    ftpd_jcd_init(&st);
    CHECK(feed("//T JOB") == FTPD_JCD_DONE && card_is("//T JOB ,"),
          "JOB without operands: the comma stands behind a blank");
    ftpd_jcd_init(&st);
    CHECK(feed("//T JOB   ") == FTPD_JCD_DONE && card_is("//T JOB ,"),
          "JOB with blanks only");

    /* a card that runs to column 71 has no room (#383) */
    ftpd_jcd_init(&st);
    memset(buf, ' ', 80); buf[80] = '\0';
    memcpy(buf, "//J JOB ", 8);
    fill(buf, 8, 63, 'A');          /* operands to column 71, no comma */
    CHECK(feed(buf) == FTPD_JCD_REFUSE, "no room for the comma");
    CHECK(feed("//STEP EXEC PGM=X") == FTPD_JCD_REFUSE,
          "nothing is accepted after a refusal");

    ftpd_jcd_init(&st);
    memset(buf, ' ', 80); buf[80] = '\0';
    memcpy(buf, "//J JOB ", 8);
    fill(buf, 8, 62, 'A');          /* operands to column 70 */
    CHECK(feed(buf) == FTPD_JCD_DONE && card[70] == ',',
          "comma fits in column 71");

    ftpd_jcd_init(&st);
    memset(buf, ' ', 80); buf[80] = '\0';
    memcpy(buf, "//J JOB ", 8);
    fill(buf, 8, 63, 'A');
    buf[70] = ',';
    CHECK(feed(buf) == FTPD_JCD_HOLD,
          "comma in column 71 continues, needs no room");

    ftpd_jcd_init(&st);
    memset(buf, ' ', 80); buf[80] = '\0';
    memcpy(buf, "//J JOB A", 9);
    fill(buf, 72, 8, '1');          /* sequence field */
    CHECK(feed(buf) == FTPD_JCD_DONE && card[9] == ',' && card[72] == '1',
          "columns 73-80 are left alone");

    /* a card that does not belong to the statement (#383) */
    ftpd_jcd_init(&st);
    CHECK(feed("//J JOB (A),") == FTPD_JCD_HOLD, "open statement");
    CHECK(feed("//STEP1 EXEC PGM=IEFBR14") == FTPD_JCD_REFUSE,
          "a statement after a comma is no continuation");

    ftpd_jcd_init(&st);
    feed("//J JOB (A),");
    CHECK(feed("/*") == FTPD_JCD_REFUSE, "delimiter");
    ftpd_jcd_init(&st);
    feed("//J JOB (A),");
    CHECK(feed("//") == FTPD_JCD_REFUSE, "null statement");
    ftpd_jcd_init(&st);
    feed("//J JOB (A),");
    CHECK(feed("DATA") == FTPD_JCD_REFUSE, "data");
    ftpd_jcd_init(&st);
    feed("//J JOB (A),");
    CHECK(feed("") == FTPD_JCD_REFUSE, "blank card");
    ftpd_jcd_init(&st);
    feed("//J JOB (A),");
    CHECK(feed("//                X") == FTPD_JCD_REFUSE,
          "operand behind column 16 is no continuation");
    ftpd_jcd_init(&st);
    feed("//J JOB (A),");
    CHECK(feed("//") == FTPD_JCD_REFUSE && ftpd_jcd_unfinished(&st) == 0,
          "refused is not unfinished");

    /* quotes and parentheses */
    ftpd_jcd_init(&st);
    CHECK(feed("//J JOB (A),'MY NAME',CLASS=A") == FTPD_JCD_DONE &&
          card_is("//J JOB (A),'MY NAME',CLASS=A,"),
          "blank inside quotes does not end the operands");
    ftpd_jcd_init(&st);
    CHECK(feed("//J JOB (A),'O''BRIEN X',CLASS=A") == FTPD_JCD_DONE &&
          card_is("//J JOB (A),'O''BRIEN X',CLASS=A,"), "doubled quote");
    ftpd_jcd_init(&st);
    CHECK(feed("//J JOB (A),'A, B ,CLASS=A") == FTPD_JCD_HOLD &&
          st.inquote == 1, "open quote: the string runs on");
    CHECK(feed("//              C',CLASS=A") == FTPD_JCD_DONE &&
          card_is("//              C',CLASS=A,"),
          "string continues in column 16");
    ftpd_jcd_init(&st);
    CHECK(feed("//J JOB (A,") == FTPD_JCD_HOLD && st.depth == 1,
          "open parenthesis continues");
    CHECK(feed("//   B),CLASS=A") == FTPD_JCD_DONE && st.depth == 0,
          "closed on the next card");
    ftpd_jcd_init(&st);
    CHECK(feed("//J JOB (A B),X") == FTPD_JCD_REFUSE,
          "operands end inside parentheses: no comma in there");

    /* NOTIFY */
    ftpd_jcd_init(&st);
    feed("//J JOB A,NOTIFY=ME");
    CHECK(st.has_notify == 1, "NOTIFY in the operands");
    ftpd_jcd_init(&st);
    feed("//J JOB A   NOTIFY ME");
    CHECK(st.has_notify == 0, "NOTIFY in the comment field");
    ftpd_jcd_init(&st);
    feed("//J JOB A,");
    feed("//  NOTIFY=ME");
    CHECK(st.has_notify == 1, "NOTIFY on a continuation line");

    /* limit */
    ftpd_jcd_init(&st);
    {
        int n;
        int rc = FTPD_JCD_HOLD;

        feed("//J JOB A,");
        for (n = 2; n < FTPD_JCD_MAXCARDS && rc == FTPD_JCD_HOLD; n++)
            rc = feed("//  B,");
        CHECK(rc == FTPD_JCD_HOLD && st.nheld == FTPD_JCD_MAXCARDS - 1,
              "one below the limit");
        CHECK(feed("//  C") == FTPD_JCD_DONE &&
              st.nheld == FTPD_JCD_MAXCARDS,
              "the last card fits at the limit");
    }
    ftpd_jcd_init(&st);
    {
        int n;
        int rc = FTPD_JCD_HOLD;

        feed("//J JOB A,");
        for (n = 1; n < FTPD_JCD_MAXCARDS; n++)
            rc = feed("//  B,");
        CHECK(rc == FTPD_JCD_HOLD && st.nheld == FTPD_JCD_MAXCARDS,
              "limit reached");
        CHECK(feed("//  C") == FTPD_JCD_REFUSE && card_is("//  C"),
              "one more is refused and not changed");
    }

    /* no JOB statement at all: nothing to track */
    ftpd_jcd_init(&st);
    CHECK(feed("//STEP1 EXEC PGM=IEFBR14") == FTPD_JCD_PASS &&
          ftpd_jcd_unfinished(&st) == 0, "stream without a JOB card");

    CHECK(ftpd_jcd_feed(NULL, card, 80) == FTPD_JCD_REFUSE, "NULL state");
    ftpd_jcd_init(&st);
    CHECK(ftpd_jcd_feed(&st, NULL, 80) == FTPD_JCD_REFUSE, "NULL card");
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

    test_statement_end();

    return mbt_test_summary("TSTJCD");
}
