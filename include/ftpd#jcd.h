#ifndef FTPD_JCD_H
#define FTPD_JCD_H
/*
** FTPD JOB statement recognition
**
** STOR into the JES view injects NOTIFY, USER and PASSWORD behind the JOB
** statement of the stream.  Taking any card that merely contains " JOB " for
** that statement put the credentials behind a comment card and left the real
** JOB statement without them (#277).  The test is free of project and MVS
** headers so a host unit test can link it (test/tstjcd.c).
*/

/* Is card (len bytes, in the runtime character set, not necessarily NUL
** terminated) a JOB statement: "//", a name of one to eight alphanumeric or
** national characters (A-Z 0-9 @ # $) starting in column 3, one or more
** blanks, then the operation JOB followed by a blank or the end of the card?
** Comment cards (slash, slash, asterisk), the null statement, the delimiter
** card and data are not.  NULL and len <= 0 are not JOB statements. */
int ftpd_jcd_is_job(const char *card, int len)              asm("FTPJCDJB");

#endif /* FTPD_JCD_H */
