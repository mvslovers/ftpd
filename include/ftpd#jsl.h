#ifndef FTPD_JSL_H
#define FTPD_JSL_H
/*
** FTPD spool file table of LIST JOBnnnnn
**
** In the JES mode, LIST with a job ID answers with the z/OS form: the job
** header and the job line, a line of dashes, then a table of the spool files
** that starts with an ID column, and the count of the files.  Clients such as
** the Zowe FTP plug-in find the table by the line that starts with blanks and
** "ID" and read the columns by the position of the header words (#370).  The
** lines live here, free of project and MVS headers so a host unit test can
** link them (test/tstjsl.c).
**
** Every function writes one line without a line end into buf (at most bufsz
** bytes with the NUL) and returns its length, or -1 if it does not fit.
*/

/* The most spool files of one job the table can number; the ID has three
** digits like the ID of the table on z/OS and stops at 999. */
#define FTPD_JSL_MAXID 999

/* The line of dashes between the job line and the table. */
int ftpd_jsl_rule(char *buf, unsigned bufsz)                asm("FTPJSLRL");

/* The header of the table.  It ends in one blank: a client that cuts the
** last column at the last character of the header would lose a digit of
** BYTE-COUNT without it. */
int ftpd_jsl_head(char *buf, unsigned bufsz)                asm("FTPJSLHD");

/* One spool file.  id is the number RETR takes after the point (1 up to
** FTPD_JSL_MAXID).  step and proc are the names as the spool holds them, ""
** for none; a file that JES2 made itself (jes2 set) with no step is given the
** step JES2, as on z/OS.  cls is the output class, 0 or blank for none.
** The caller makes the names and cls printable (ftpd_jwn_printable()).
** ddname is cut to 8 characters, bytes is capped at 2147483647 (10 digits). */
int ftpd_jsl_row(char *buf, unsigned bufsz, int id, const char *step,
                 const char *proc, char cls, const char *ddname, long bytes,
                 int jes2)                                  asm("FTPJSLRW");

/* The last line: the number of spool files, "3 spool files".  The word is
** plural for one file too, as clients match "spool files". */
int ftpd_jsl_tail(char *buf, unsigned bufsz, int count)     asm("FTPJSLTL");

#endif /* FTPD_JSL_H */
