/*
** FTPD JOB statement recognition
**
** See include/ftpd#jcd.h.
*/
#include <string.h>

#include "ftpd#jcd.h"

#define JCD_NAME_MAX 8

/* Spelled out: the EBCDIC letters are not contiguous, so no range test. */
static const char jcd_namechars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789@#$";

int
ftpd_jcd_is_job(const char *card, int len)
{
    int i;
    int n;

    if (!card || len < 3)
        return 0;

    if (card[0] != '/' || card[1] != '/')
        return 0;

    i = 2;
    n = 0;
    while (i < len && card[i] != '\0' && strchr(jcd_namechars, card[i])) {
        i++;
        n++;
    }
    if (n < 1 || n > JCD_NAME_MAX)
        return 0;

    if (i >= len || card[i] != ' ')
        return 0;
    while (i < len && card[i] == ' ')
        i++;

    if (len - i < 3 || card[i] != 'J' || card[i + 1] != 'O' ||
        card[i + 2] != 'B')
        return 0;
    i += 3;

    return i == len || card[i] == ' ';
}
