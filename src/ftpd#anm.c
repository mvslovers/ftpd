/*
** FTPD ACEE user name check
**
** See include/ftpd#anm.h.
*/
#include <stddef.h>
#include <ctype.h>

#include "ftpd#anm.h"

#define ANM_MAXLEN  8

/* ACEEUSER is a length byte, then the name blank padded. */
static unsigned
name_length(const char *aceeuser)
{
    unsigned alen = (unsigned char)aceeuser[0];

    if (alen > ANM_MAXLEN)
        alen = ANM_MAXLEN;
    while (alen && aceeuser[alen] == ' ')
        alen--;
    return alen;
}

void
ftpd_acee_user_name(char *out, const char *aceeuser)
{
    unsigned len = aceeuser ? name_length(aceeuser) : 0;
    unsigned i;

    for (i = 0; i < len; i++)
        out[i] = aceeuser[1 + i];
    out[len] = '\0';
}

int
ftpd_acee_user_matches(const char *aceeuser, const char *userid)
{
    unsigned alen;
    unsigned ulen;
    unsigned i;

    if (!aceeuser || !userid)
        return 0;

    alen = name_length(aceeuser);

    for (ulen = 0; ulen < ANM_MAXLEN && userid[ulen]; ulen++)
        ;
    while (ulen && userid[ulen - 1] == ' ')
        ulen--;

    if (alen == 0 || alen != ulen)
        return 0;

    for (i = 0; i < alen; i++) {
        if (toupper((unsigned char)aceeuser[1 + i])
            != toupper((unsigned char)userid[i]))
            return 0;
    }
    return 1;
}
