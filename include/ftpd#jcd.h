#ifndef FTPD_JCD_H
#define FTPD_JCD_H
/*
** FTPD JOB statement recognition and tracking
**
** STOR into the JES view injects NOTIFY, USER and PASSWORD behind the JOB
** statement of the stream.  Taking any card that merely contains " JOB " for
** that statement put the credentials behind a comment card and left the real
** JOB statement without them (#277).  The test is free of project and MVS
** headers so a host unit test can link it (test/tstjcd.c).
**
** Where the statement ends decides where the credentials may go.  The operand
** field of a card ends at the first blank outside quotes and parentheses;
** only a comma as its last character continues the statement.  A comma in
** the comment field behind the operands continues nothing, and a card whose
** operands run to column 71 has no room for the comma that would continue it
** (#383).  In both cases the cards injected behind it are not part of the
** JOB statement, the job has no owner and PASSWORD= stands in the spool in
** the clear.  So the cards of the JOB statement are held back until the end
** of the statement is known, and a statement that cannot be continued is
** refused before anything of it reaches the internal reader.
*/

/* Most cards a JOB statement may have, comment cards between continuation
** lines included; the caller keeps that many 80-byte cards.  A longer
** statement is refused. */
#define FTPD_JCD_MAXCARDS 16

/* What the caller does with the card it just fed. */
#define FTPD_JCD_REFUSE   (-1)  /* fail closed: submit nothing of the JOB */
#define FTPD_JCD_PASS     0     /* not part of the JOB statement: put it */
#define FTPD_JCD_HOLD     1     /* part of it, more follow: keep it */
#define FTPD_JCD_DONE     2     /* last card of it, comma added in place:
                                ** keep it, put all kept cards, inject */

typedef struct ftpd_jcd {
    int phase;          /* 0 before the JOB statement, 1 inside, 2 behind */
    int inquote;        /* a quoted string runs on to the next card */
    int depth;          /* open parentheses */
    int nheld;          /* cards HOLD or DONE returned so far */
    int has_notify;     /* a card of the statement contains NOTIFY */
} ftpd_jcd_t;

/* Is card (len bytes, in the runtime character set, not necessarily NUL
** terminated) a JOB statement: "//", a name of one to eight alphanumeric or
** national characters (A-Z 0-9 @ # $) starting in column 3, one or more
** blanks, then the operation JOB followed by a blank or the end of the card?
** Comment cards (slash, slash, asterisk), the null statement, the delimiter
** card and data are not.  NULL and len <= 0 are not JOB statements. */
int ftpd_jcd_is_job(const char *card, int len)              asm("FTPJCDJB");

/* Start a stream. */
void ftpd_jcd_init(ftpd_jcd_t *st)                          asm("FTPJCDIN");

/* Feed the next card (80 bytes, runtime character set) of the stream.  A card
** that returns HOLD or DONE is the nheld-th (counting from one) card of the
** statement; the caller keeps it at that place.  DONE has added the comma that
** continues the statement to the card.  Once REFUSE is returned the state is
** void.  card is changed only by DONE. */
int ftpd_jcd_feed(ftpd_jcd_t *st, char *card, int len)      asm("FTPJCDFD");

/* Has the stream ended inside the JOB statement?  Then the held cards are
** not a complete statement and must not be put. */
int ftpd_jcd_unfinished(const ftpd_jcd_t *st)               asm("FTPJCDUF");

#endif /* FTPD_JCD_H */
