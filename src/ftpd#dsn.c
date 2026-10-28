/*
** FTPD Data Set Name Validation
**
** See include/ftpd#dsn.h.  The set of legal characters is spelled out as a
** string and searched, rather than tested with ranges: A-Z is not contiguous
** in EBCDIC (C1-C9, D1-D9, E2-E9), so `c >= 'A' && c <= 'Z'` would accept the
** bytes in the gaps -- 0xCA-0xCF, 0xDA-0xDF -- plus '\' (0xE0), '{' (0xC0)
** and '}' (0xD0).  The table costs 41 bytes and is right in both encodings.
*/
#include <string.h>

#include "ftpd#dsn.h"

/* Every character an MVS data set name qualifier may hold.  The '.' that
** separates qualifiers and the wildcards are handled separately below. */
static const char dsn_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789@#$-";

/* --------------------------------------------------------------------
** Validate a data set name.  See ftpd#dsn.h for the contract.
** ----------------------------------------------------------------- */
int
ftpd_dsn_valid(const char *dsn, int allow_wildcards)
{
    const char *p;
    int qlen;               /* characters in the qualifier being read */
    int mlen;               /* characters in the member name read so far */

    if (!dsn)
        return 0;

    qlen = 0;
    for (p = dsn; *p && *p != '('; p++) {
        if (*p == '.') {
            /* Nothing between two dots is not a qualifier.  `put .profile`
            ** resolved to HERC01..PROFILE and got as far as SVC 99. */
            if (qlen == 0)
                return 0;
            qlen = 0;
            continue;
        }

        if (allow_wildcards && strchr(FTPD_DSN_WILDCARDS, *p) != NULL) {
            qlen++;
            continue;
        }

        if (strchr(dsn_chars, *p) == NULL)
            return 0;

        qlen++;
    }

    /* qlen is the last qualifier's length: 0 means the name ended on a dot,
    ** was empty, or was nothing but a member -- "(MEM)". */
    if (qlen == 0)
        return 0;

    if (*p == '\0')
        return 1;

    /* *p is the '(' of a member.  What follows is read to the end: the
    ** member, a closing parenthesis, and nothing else. */
    p++;
    mlen = 0;
    for (; *p && *p != ')'; p++) {
        if (*p == '.') {
            /* An extension (`DSN(name.ext)`, from a client that sends file
            ** names).  sanitize_member() cuts it off, but only after the
            ** characters have been looked at here. */
            if (mlen == 0)
                return 0;
            continue;
        }

        if (allow_wildcards && strchr(FTPD_DSN_WILDCARDS, *p) != NULL) {
            mlen++;
            continue;
        }

        if (strchr(dsn_chars, *p) == NULL)
            return 0;

        mlen++;
    }

    /* Closed, and nothing after the closing parenthesis. */
    return (*p == ')' && p[1] == '\0' && mlen > 0);
}

/* --------------------------------------------------------------------
** Does the name carry a member?  See ftpd#dsn.h for the contract.
** ----------------------------------------------------------------- */
int
ftpd_dsn_has_member(const char *dsn)
{
    return dsn != NULL && strchr(dsn, '(') != NULL;
}

/* --------------------------------------------------------------------
** Is every '**' a qualifier of its own?  See ftpd#dsn.h for the contract.
** ----------------------------------------------------------------- */
int
ftpd_dsn_stars_ok(const char *pattern)
{
    const char *p;
    const char *run;

    if (!pattern)
        return 1;

    for (p = pattern; *p; ) {
        if (*p != '*') {
            p++;
            continue;
        }

        run = p;
        while (*p == '*')
            p++;

        if (p - run == 1)
            continue;
        if (p - run > 2)
            return 0;
        if (run != pattern && run[-1] != '.')
            return 0;
        if (*p != '\0' && *p != '.')
            return 0;
    }

    return 1;
}
