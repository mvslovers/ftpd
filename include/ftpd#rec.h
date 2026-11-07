#ifndef FTPD_REC_H
#define FTPD_REC_H
/*
** FTPD Record Fit
**
** A text line stored with STOR/APPE is one record.  When the line is longer
** than the data set's record length, z/OS FTP fails the transfer, or -- with
** SITE TRUNCATE -- cuts the line and says so (#207).  The size arithmetic
** lives here, free of project and MVS headers so a host unit test can link
** it (test/tstrec.c).
*/

/* RECFM family of the data set */
#define FTPD_REC_F  'F'
#define FTPD_REC_V  'V'
#define FTPD_REC_U  'U'
#define FTPD_REC_VS 'S'     /* V with the spanned bit: VBS, or VB that OPEN made spanned */

/* Most data bytes one record of the data set holds, at most cap.
**
** F: lrecl.  V: lrecl - 4, because LRECL counts the 4-byte RDW the program
** never sees (a V data set with LRECL 1-4 holds none, so 0).  VS: lrecl - 8;
** libc370's @@AWRITE refuses a spanned record whose RDW + data + 4 exceeds
** LRECL with ABEND 002, so the data stops 4 bytes earlier (#432).  U: blksize,
** since there is no LRECL.  -1 when the data set reports no length at all. */
int ftpd_rec_maxdata(int recfm, int lrecl, int blksize, int cap)
                                                            asm("FTPRECMX");

#endif /* FTPD_REC_H */
