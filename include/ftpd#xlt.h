#ifndef FTPD_XLT_H
#define FTPD_XLT_H
/*
** FTPD EBCDIC <-> ASCII Translation
*/

/* Translation table pointers (defined in ftpd#xlt.c) */
extern unsigned char *asc2ebc;
extern unsigned char *ebc2asc;

/* In-place buffer translation */
void ftpd_xlat_a2e(unsigned char *buf, int len)             asm("FTPXLA2E");
void ftpd_xlat_e2a(unsigned char *buf, int len)             asm("FTPXLE2A");

#endif /* FTPD_XLT_H */
