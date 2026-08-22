/*
** FTPD Address Parameter Parsing
**
** See include/ftpd#adr.h.  Hand-rolled rather than sscanf("%u.%u.%u.%u"):
** sscanf reports four conversions for "1.2.3.4junk" and for "999.1.2.3" as
** well, which is how an out-of-range or mistyped address used to reach the
** socket layer unnoticed.  The loop below costs less code than the sscanf
** call it replaces and rejects both.
**
** '0'-'9' are contiguous in EBCDIC as well as ASCII, so the digit test and
** the '0' subtraction are encoding independent.
*/
#include <string.h>

#include "ftpd#adr.h"

/* --------------------------------------------------------------------
** The literal ANY, in any case.  Spelled out instead of strcasecmp()
** (not in the cc370 sysroot) or tolower() (would pull in ctype).
** ----------------------------------------------------------------- */
static int
is_any(const char *s)
{
    return (s[0] == 'A' || s[0] == 'a') &&
           (s[1] == 'N' || s[1] == 'n') &&
           (s[2] == 'Y' || s[2] == 'y') &&
            s[3] == '\0';
}

/* --------------------------------------------------------------------
** Parse an address parameter.  See ftpd#adr.h for the contract.
** ----------------------------------------------------------------- */
int
ftpd_adr_parse(const char *value, char *out, unsigned *addr)
{
    unsigned    octet[4];
    const char *p;
    int         i;
    int         digits;
    size_t      len;

    if (!value)
        return FTPD_ADR_BAD;

    if (is_any(value)) {
        if (out)
            strcpy(out, "ANY");
        if (addr)
            *addr = 0;
        return FTPD_ADR_ANY;
    }

    /* Reject before parsing what would not survive being stored: a value
    ** longer than the buffer used to be truncated into a valid but
    ** different address (192.168.100.2000 -> 192.168.100.200). */
    len = strlen(value);
    if (len == 0 || len >= FTPD_ADR_SIZE)
        return FTPD_ADR_BAD;

    p = value;
    for (i = 0; i < 4; i++) {
        unsigned v = 0;

        for (digits = 0; *p >= '0' && *p <= '9'; digits++, p++) {
            if (digits == 3)
                return FTPD_ADR_BAD;
            v = v * 10 + (unsigned)(*p - '0');
        }
        if (digits == 0 || v > 255)
            return FTPD_ADR_BAD;

        octet[i] = v;

        if (i < 3) {
            if (*p != '.' && *p != ',')
                return FTPD_ADR_BAD;
            p++;
        }
    }

    /* Four octets parsed -- anything left is garbage, not an address. */
    if (*p != '\0')
        return FTPD_ADR_BAD;

    if (out) {
        for (i = 0; value[i]; i++)
            out[i] = (value[i] == ',') ? '.' : value[i];
        out[i] = '\0';
    }

    if (addr)
        *addr = (octet[0] << 24) | (octet[1] << 16) |
                (octet[2] <<  8) |  octet[3];

    return FTPD_ADR_OK;
}

/* --------------------------------------------------------------------
** Do two bind addresses on the same port collide?  See ftpd#adr.h.
** ----------------------------------------------------------------- */
int
ftpd_adr_conflicts(unsigned bound, unsigned want)
{
    return bound == 0 || want == 0 || bound == want;
}
