/* TSTREC.C
** Tests for ftpd_rec_maxdata() -- how much of a text line
** fits in one record (#207).  STOR used to cut an overlong line without a
** word and answer 250.
**
** ftpd#rec.c is free of project and MVS headers, so this is a DUAL test.
*/
#include <stdio.h>

#include "ftpd#rec.h"
#include <mbtcheck.h>

int
main(void)
{
    CHECK_EQ(ftpd_rec_maxdata(FTPD_REC_F, 80, 3120, 32767), 80,
             "F: LRECL");
    CHECK_EQ(ftpd_rec_maxdata(FTPD_REC_V, 255, 3120, 32767), 251,
             "V: LRECL less the RDW");
    CHECK_EQ(ftpd_rec_maxdata(FTPD_REC_U, 0, 6144, 32767), 6144,
             "U: BLKSIZE");
    CHECK_EQ(ftpd_rec_maxdata(FTPD_REC_F, 40000, 0, 32767), 32767,
             "capped by the buffer");
    CHECK_EQ(ftpd_rec_maxdata(FTPD_REC_V, 4, 0, 32767), 0,
             "V with LRECL 4 holds no data");
    CHECK_EQ(ftpd_rec_maxdata(FTPD_REC_V, 2, 0, 32767), 0,
             "V never goes negative");
    CHECK_EQ(ftpd_rec_maxdata(FTPD_REC_V, 600, 259, 32767), 596,
             "V: LRECL 600 holds 596 data bytes, 592 + 4 is not the limit");
    CHECK_EQ(ftpd_rec_maxdata(FTPD_REC_VS, 600, 259, 32767), 592,
             "VBS: LRECL less RDW and SDW, 592 of 600 (#432)");
    CHECK_EQ(ftpd_rec_maxdata(FTPD_REC_VS, 600, 259, 592), 592,
             "VBS: the cap does not change the limit");
    CHECK_EQ(ftpd_rec_maxdata(FTPD_REC_VS, 255, 6233, 32767), 247,
             "VBS 255 takes 247");
    CHECK_EQ(ftpd_rec_maxdata(FTPD_REC_VS, 8, 0, 32767), 0,
             "VBS with LRECL 8 holds no data");
    CHECK_EQ(ftpd_rec_maxdata(FTPD_REC_VS, 5, 0, 32767), 0,
             "VBS never goes negative");
    CHECK_EQ(ftpd_rec_maxdata(FTPD_REC_VS, 0, 0, 32767), -1,
             "VBS with no LRECL: length unknown, not zero");
    CHECK_EQ(ftpd_rec_maxdata(FTPD_REC_VS, 40000, 0, 32767), 32767,
             "VBS capped by the buffer");
    CHECK_EQ(ftpd_rec_maxdata(FTPD_REC_F, 0, 0, 32767), -1,
             "F with no LRECL: length unknown, not zero");
    CHECK_EQ(ftpd_rec_maxdata(FTPD_REC_V, 0, 0, 32767), -1,
             "V with no LRECL: length unknown, not zero");
    CHECK_EQ(ftpd_rec_maxdata(FTPD_REC_U, 80, 0, 32767), -1,
             "U with no BLKSIZE: length unknown");

    return mbt_test_summary("TSTREC");
}
