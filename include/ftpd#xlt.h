#ifndef FTPD_XLT_H
#define FTPD_XLT_H
/*
** FTPD EBCDIC <-> ASCII Translation
**
** Two codepages:
**   IBM1047 — FTP control/data, UFS file content (default)
**   CP037   — MVS dataset content
**
** The "active" pointers (asc2ebc / ebc2asc) are set at startup
** via ftpd_xlat_init() and used for FTP protocol I/O.
** For MVS dataset transfers, use ftpd_cp037_atoe / ftpd_cp037_etoa
** directly.
*/

/* Codepage pair */
typedef struct ftpd_cp {
    const unsigned char *atoe;
    const unsigned char *etoa;
} ftpd_cp_t;

/* Named codepage pairs (defined in ftpd#xlt.c) */
extern ftpd_cp_t ftpd_cp037;
extern ftpd_cp_t ftpd_cp1047;

/* Active translation table pointers (set by ftpd_xlat_init) */
extern unsigned char *asc2ebc;
extern unsigned char *ebc2asc;

/* Convenience pointers for MVS dataset translation */
extern const unsigned char *ftpd_cp037_atoe;
extern const unsigned char *ftpd_cp037_etoa;

/* Initialize translation tables.
** codepage: "CP037" or "IBM1047" (case insensitive).
** NULL defaults to IBM1047.
** Returns 0 on success, -1 on unknown codepage.
*/
int ftpd_xlat_init(const char *codepage)                        asm("FTPXLINI");

/* In-place buffer translation (uses active tables) */
void ftpd_xlat_a2e(unsigned char *buf, int len)                 asm("FTPXLA2E");
void ftpd_xlat_e2a(unsigned char *buf, int len)                 asm("FTPXLE2A");

#endif /* FTPD_XLT_H */
