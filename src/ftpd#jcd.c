/*
** FTPD JOB statement recognition and tracking
**
** See include/ftpd#jcd.h.
*/
#include <string.h>

#include "ftpd#jcd.h"

#define JCD_NAME_MAX 8

/* Columns 1-71 carry a statement.  Column 72 marks a continuation and 73-80
** are the sequence field.  A continuation comma stands in column 71 at the
** latest, the index of that column is 70. */
#define JCD_CONTENT     71
#define JCD_COMMA_MAX   70
/* A quoted string that runs on to the next card goes on in column 16. */
#define JCD_QUOTE_CONT  15
#define JCD_CONT_START  15

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

void
ftpd_jcd_init(ftpd_jcd_t *st)
{
    if (st)
        memset(st, 0, sizeof(*st));
}

int
ftpd_jcd_unfinished(const ftpd_jcd_t *st)
{
    return st && st->phase == 1;
}

/* A comment card inside a statement is no end of it. */
static int
jcd_is_comment(const char *card, int len)
{
    return len >= 3 && card[0] == '/' && card[1] == '/' && card[2] == '*';
}

static int
jcd_refuse(ftpd_jcd_t *st)
{
    st->phase = 3;
    return FTPD_JCD_REFUSE;
}

static int
jcd_hold(ftpd_jcd_t *st)
{
    if (st->nheld >= FTPD_JCD_MAXCARDS)
        return jcd_refuse(st);
    st->nheld++;
    return FTPD_JCD_HOLD;
}

/* NOTIFY= among the operands [from, to) of a card. */
static int
jcd_has_notify(const char *card, int from, int to)
{
    int i;

    for (i = from; i + 6 <= to; i++) {
        if (memcmp(card + i, "NOTIFY", 6) == 0)
            return 1;
    }
    return 0;
}

int
ftpd_jcd_feed(ftpd_jcd_t *st, char *card, int len)
{
    int i;
    int start;
    int lim;
    int end;
    int last;
    int first;
    int jobend = 0;

    if (!st || !card || len < 0 || st->phase == 3)
        return st ? jcd_refuse(st) : FTPD_JCD_REFUSE;

    if (st->phase == 2)
        return FTPD_JCD_PASS;

    first = 0;
    if (st->phase == 0) {
        if (!ftpd_jcd_is_job(card, len))
            return FTPD_JCD_PASS;
        st->phase = 1;
        first = 1;
        i = 2;
        while (card[i] != ' ')
            i++;
        while (card[i] == ' ')
            i++;
        i += 3;                 /* behind JOB */
        jobend = i;
    } else {
        if (jcd_is_comment(card, len))
            return jcd_hold(st);
        if (len < 3 || card[0] != '/' || card[1] != '/' || card[2] != ' ')
            return jcd_refuse(st);
        if (st->inquote) {
            i = JCD_QUOTE_CONT;
        } else {
            i = 3;
            while (i < len && card[i] == ' ')
                i++;
            if (i >= len || i > JCD_CONT_START)
                return jcd_refuse(st);
        }
    }

    lim = len < JCD_CONTENT ? len : JCD_CONTENT;

    if (first) {
        while (i < lim && card[i] == ' ')
            i++;
    }
    start = i;

    if (first && i >= lim) {
        /* JOB without operands: the comma goes behind one blank. */
        end = jobend + 1;
    } else {
        for (; i < lim; i++) {
            char c = card[i];

            if (st->inquote) {
                if (c == '\'') {
                    if (i + 1 < lim && card[i + 1] == '\'')
                        i++;
                    else
                        st->inquote = 0;
                }
            } else if (c == '\'') {
                st->inquote = 1;
            } else if (c == '(') {
                st->depth++;
            } else if (c == ')') {
                if (st->depth > 0)
                    st->depth--;
            } else if (c == ' ') {
                break;
            }
        }

        if (jcd_has_notify(card, start, i))
            st->has_notify = 1;

        if (st->inquote)
            return jcd_hold(st);
        if (i > start && card[i - 1] == ',')
            return jcd_hold(st);

        /* The operand field ends here and says nothing of a continuation. */
        if (st->depth > 0)
            return jcd_refuse(st);
        end = i;
    }

    if (end > JCD_COMMA_MAX || end >= len || st->nheld >= FTPD_JCD_MAXCARDS)
        return jcd_refuse(st);

    /* A single blank before a comment: the comma must not glue the comment
    ** to the operands.  Move it by one, or drop it when it would reach
    ** column 72. */
    if (end + 1 < len && card[end + 1] != ' ') {
        last = len - 1;
        while (last > end && card[last] == ' ')
            last--;
        if (last + 1 <= JCD_COMMA_MAX)
            memmove(card + end + 2, card + end + 1, (size_t)(last - end));
        else
            memset(card + end + 1, ' ', (size_t)(len - end - 1));
        card[end + 1] = ' ';
    }
    card[end] = ',';

    st->nheld++;
    st->phase = 2;
    return FTPD_JCD_DONE;
}
