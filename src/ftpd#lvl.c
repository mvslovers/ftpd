/*
** FTPD LIST/NLST Data Set Pattern and Catalog Level
**
** See include/ftpd#lvl.h.
*/
#include <ctype.h>
#include <stddef.h>
#include <string.h>

#include "ftpd#dsn.h"
#include "ftpd#lvl.h"

/* The recursion consumes one '**' of the pattern per level, so its depth is
** the number of '**' in a pattern of at most 44 characters: 15 at the most.
** The run time is another matter: '**' between literals tries the rest of the
** pattern at every qualifier boundary, so it costs every way of splitting
** the name over them.
** The calls are counted in *left, which lives on the caller's stack, and the
** match gives up with "no match" when they run out. */
static int
dsn_match(const char *pattern, const char *name, int *left)
{
    const char *p = pattern;
    const char *n = name;

    if (*left <= 0)
        return 0;
    (*left)--;

    while (*p && *n) {
        if (*p == '*' && *(p + 1) == '*') {
            /* ** = match any number of qualifiers, also none */
            p += 2;
            if (*p == '.')
                p++;  /* skip dot after ** */
            /* "**.**" is "**": n '**' in a row would cost every way of
            ** splitting the name n ways. */
            while (p[0] == '*' && p[1] == '*' &&
                   (p[2] == '.' || p[2] == '\0')) {
                p += 2;
                if (*p == '.')
                    p++;
            }
            if (*p == '\0')
                return 1;  /* ** at end matches everything */

            /* Try matching rest of pattern at each qualifier boundary */
            while (*n && *left > 0) {
                if (dsn_match(p, n, left))
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
ftpd_dsn_match_limit(const char *pattern, const char *name, int limit,
                     int *used)
{
    int left = limit;
    int rc = dsn_match(pattern, name, &left);

    if (used)
        *used = limit - left;
    return rc;
}

int
ftpd_dsn_match(const char *pattern, const char *name)
{
    return ftpd_dsn_match_limit(pattern, name, FTPD_DSN_MATCH_LIMIT, NULL);
}

int
ftpd_lvl_of(const char *pattern, char *level, int levelsz)
{
    const char *wild;
    const char *cut;
    char tmp[48];
    int len;

    if (!pattern || !pattern[0])
        return 0;

    /* A '(' makes this a member form that ftpd_lmp_split() did not accept,
    ** and what follows it was never checked. */
    if (strchr(pattern, '(') != NULL)
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

    if (len <= 0 || len >= levelsz || len >= (int)sizeof(tmp))
        return 0;

    /* The level goes into LISTC LEVEL('...'): only a plain name gets there. */
    memcpy(tmp, pattern, len);
    tmp[len] = '\0';
    if (!ftpd_dsn_valid(tmp, 0))
        return 0;

    memcpy(level, pattern, len);
    level[len] = '\0';
    return 1;
}

int
ftpd_lvl_keep(const char *filter, int has_wildcard, const char *dsn)
{
    size_t len;

    if (!filter)
        return 1;
    if (has_wildcard)
        return ftpd_dsn_match(filter, dsn);

    len = strlen(filter);
    return strcmp(filter, dsn) == 0 ||
           (strncmp(filter, dsn, len) == 0 && dsn[len] == '.');
}
