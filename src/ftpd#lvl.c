/*
** FTPD LIST/NLST Data Set Pattern and Catalog Level
**
** See include/ftpd#lvl.h.
*/
#include <ctype.h>
#include <string.h>

#include "ftpd#lvl.h"

/* The recursion consumes one '**' of the pattern per level, so its depth is
** the number of '**' in a pattern of at most 44 characters: 15 at the most. */
int
ftpd_dsn_match(const char *pattern, const char *name)
{
    const char *p = pattern;
    const char *n = name;

    while (*p && *n) {
        if (*p == '*' && *(p + 1) == '*') {
            /* ** = match any number of qualifiers, also none */
            p += 2;
            if (*p == '.')
                p++;  /* skip dot after ** */
            if (*p == '\0')
                return 1;  /* ** at end matches everything */

            /* Try matching rest of pattern at each qualifier boundary */
            while (*n) {
                if (ftpd_dsn_match(p, n))
                    return 1;
                /* Skip to next qualifier */
                while (*n && *n != '.')
                    n++;
                if (*n == '.')
                    n++;
            }
            return 0;
        }
        if (*p == '*') {
            /* * = match exactly one qualifier */
            p++;
            /* Skip the current qualifier in name */
            while (*n && *n != '.')
                n++;
            /* Both should be at dot or end */
            if (*p == '.' && *n == '.') {
                p++;
                n++;
                continue;
            }
            if (*p == '\0' && *n == '\0')
                return 1;
            return 0;
        }
        if (*p == '%') {
            /* % = match one character (not dot) */
            if (*n == '.' || *n == '\0')
                return 0;
            p++;
            n++;
            continue;
        }
        /* Literal match (case insensitive) */
        if (toupper((unsigned char)*p) != toupper((unsigned char)*n))
            return 0;
        p++;
        n++;
    }

    /* Both must be exhausted */
    return (*p == '\0' && *n == '\0');
}

int
ftpd_lvl_of(const char *pattern, char *level, int levelsz)
{
    const char *wild;
    const char *cut;
    int len;

    if (!pattern || !pattern[0])
        return 0;

    wild = strpbrk(pattern, "*%");
    if (wild) {
        /* Everything in front of the qualifier that holds the wildcard. */
        cut = wild;
        while (cut > pattern && *(cut - 1) != '.')
            cut--;
        if (cut == pattern)
            return 0;               /* the first qualifier is a pattern */
        len = (int)(cut - pattern) - 1;     /* without the dot before it */
    } else {
        cut = strrchr(pattern, '.');
        len = cut ? (int)(cut - pattern) : (int)strlen(pattern);
    }

    if (len <= 0 || len >= levelsz)
        return 0;

    memcpy(level, pattern, len);
    level[len] = '\0';
    return 1;
}
