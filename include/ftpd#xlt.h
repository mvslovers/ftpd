#ifndef FTPD_XLT_H
#define FTPD_XLT_H
/*
** FTPD EBCDIC <-> ASCII Translation
**
** Active tables (asc2ebc / ebc2asc): always IBM-1047.
** Used for FTP protocol I/O and UFS file content.
**
** For MVS dataset content, use the CP037 translation functions:
**   ftpd_xlat_mvs_a2e()  — ASCII -> EBCDIC CP037
**   ftpd_xlat_mvs_e2a()  — EBCDIC CP037 -> ASCII
*/

/* ASCII wire constants — use these instead of '\r'/'\n' after translation */
#define ASCII_CR   0x0D
#define ASCII_LF   0x0A
#define ASCII_CRLF "\x0D\x0A"

/* Active translation table pointers — always IBM-1047 */
extern unsigned char *asc2ebc;
extern unsigned char *ebc2asc;

/* In-place buffer translation (active tables = IBM-1047) */
void ftpd_xlat_a2e(unsigned char *buf, int len)                 asm("FTPXLA2E");
void ftpd_xlat_e2a(unsigned char *buf, int len)                 asm("FTPXLE2A");

/* In-place buffer translation (CP037 — for MVS dataset content) */
void ftpd_xlat_mvs_a2e(unsigned char *buf, int len)             asm("FTPXMA2E");
void ftpd_xlat_mvs_e2a(unsigned char *buf, int len)             asm("FTPXME2A");

#endif /* FTPD_XLT_H */
