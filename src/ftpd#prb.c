/*
** FTPD start check: the outcome of the check login
**
** See include/ftpd#prb.h.
*/
#include "ftpd#prb.h"

int
ftpd_probe_accepted(int has_acee, int racf_rc)
{
    if (has_acee)
        return 1;
    return racf_rc == 0 || racf_rc == FTPD_RACF_NOTAUTH;
}
