/*
** FTPD Record Fit
**
** See include/ftpd#rec.h.
*/
#include "ftpd#rec.h"

int
ftpd_rec_maxdata(int recfm, int lrecl, int blksize, int cap)
{
    int max;

    if (recfm == FTPD_REC_U)
        max = blksize;
    else
        max = lrecl;

    if (max <= 0)
        return -1;

    if (recfm == FTPD_REC_V || recfm == FTPD_REC_VS) {
        max -= recfm == FTPD_REC_VS ? 8 : 4;
        if (max < 0)
            max = 0;
    }
    return max > cap ? cap : max;
}
