/* TSTADR.C
** Tests for ftpd_adr_parse() -- the shared parser behind SRVBIND (SRVIP),
** PASVBIND and PASVADR (issue #76).
**
** The regression that started it: SRVIP=127,0,0,1 was copied into the config
** unchanged, sscanf("%u.%u.%u.%u") then reported one conversion instead of
** four, and the bind silently kept INADDR_ANY -- FTPD listened on every
** address of the host while the operator had asked for one.  The cases below
** pin the comma form down, plus everything else that used to pass for an
** address: truncated values, trailing garbage, octets over 255.
**
** ftpd#adr.c is free of project and MVS headers, so this is a DUAL test: it
** runs natively via `make test-host` and on MVS via `make test-mvs`.
*/
#include <stdio.h>
#include <string.h>

#include "ftpd#adr.h"
#include <mbtcheck.h>

/* An address in host byte order, for the expected values below. */
#define QUAD(a, b, c, d) \
    (((unsigned)(a) << 24) | ((b) << 16) | ((c) << 8) | (d))

/* Parse and report whether value, out and addr all came out as expected. */
static int
ok(const char *value, const char *want_out, unsigned want_addr)
{
    char     out[FTPD_ADR_SIZE];
    unsigned addr = 0xDEADBEEF;

    memset(out, '\0', sizeof(out));

    if (ftpd_adr_parse(value, out, &addr) != FTPD_ADR_OK)
        return 0;

    return strcmp(out, want_out) == 0 && addr == want_addr;
}

/* Rejected values must leave the caller's buffer and address alone, so a
** default set before the call survives. */
static int
bad(const char *value)
{
    char     out[FTPD_ADR_SIZE];
    unsigned addr = 0xDEADBEEF;

    strcpy(out, "ANY");

    if (ftpd_adr_parse(value, out, &addr) != FTPD_ADR_BAD)
        return 0;

    return strcmp(out, "ANY") == 0 && addr == 0xDEADBEEF;
}

int
main(void)
{
    char     out[FTPD_ADR_SIZE];
    unsigned addr = 0xDEADBEEF;

    printf("=== FTPD address parameter tests (#76) ===\n");

    /* --- the reported bug: comma form on a bind address --------------- */
    CHECK(ok("127,0,0,1", "127.0.0.1", QUAD(127, 0, 0, 1)),
          "comma form 127,0,0,1 parses (was silently ANY)");
    CHECK(ok("123,0,0,1", "123.0.0.1", QUAD(123, 0, 0, 1)),
          "comma form is normalised to dotted form for the config dump");

    /* --- dotted form, the boundaries ---------------------------------- */
    CHECK(ok("127.0.0.1", "127.0.0.1", QUAD(127, 0, 0, 1)),
          "dotted form 127.0.0.1 parses");
    CHECK(ok("0.0.0.0", "0.0.0.0", 0u), "0.0.0.0 parses (explicit, not ANY)");
    CHECK(ok("255.255.255.255", "255.255.255.255", 0xFFFFFFFFu),
          "255.255.255.255 parses -- longest value still fits the buffer");
    CHECK(ok("203.0.113.10", "203.0.113.10", QUAD(203, 0, 113, 10)),
          "a public address parses");
    CHECK(ok("10,1,2,3", "10.1.2.3", QUAD(10, 1, 2, 3)),
          "short octets in comma form parse");

    /* --- ANY is a decision, not an error ------------------------------ */
    memset(out, '\0', sizeof(out));
    CHECK(ftpd_adr_parse("ANY", out, &addr) == FTPD_ADR_ANY &&
          strcmp(out, "ANY") == 0 && addr == 0,
          "ANY reports FTPD_ADR_ANY and address 0");
    memset(out, '\0', sizeof(out));
    CHECK(ftpd_adr_parse("any", out, &addr) == FTPD_ADR_ANY &&
          strcmp(out, "ANY") == 0,
          "any is case-insensitive and stored uppercase");
    CHECK(ftpd_adr_parse("Any", NULL, NULL) == FTPD_ADR_ANY,
          "Any parses with both outputs omitted");

    /* --- what used to pass unnoticed ---------------------------------- */
    CHECK(bad("127.0.0"), "three octets are rejected (was silently ANY)");
    CHECK(bad("127.0.0.1.5"), "five octets are rejected");
    CHECK(bad("localhost"), "a hostname is rejected -- FTPD resolves nothing");
    CHECK(bad("1.2.3.4junk"), "trailing garbage is rejected (sscanf took it)");
    CHECK(bad("999.1.2.3"), "an octet over 255 is rejected (sscanf took it)");
    CHECK(bad("256.0.0.1"), "256 is rejected -- one past the boundary");
    CHECK(bad("192.168.100.2000"),
          "an over-long value is rejected, not truncated to a valid address");
    CHECK(bad("1.2.3."), "a missing last octet is rejected");
    CHECK(bad(".1.2.3"), "a missing first octet is rejected");
    CHECK(bad("1..2.3"), "an empty octet is rejected");
    CHECK(bad(""), "an empty value is rejected");
    CHECK(bad(" 127.0.0.1"), "a leading blank is rejected (the config trims)");
    CHECK(bad("127.0.0.1 "), "a trailing blank is rejected (the config trims)");
    CHECK(bad("1.2.3.-4"), "a negative octet is rejected");
    CHECK(bad("0000.0.0.1"), "more than three digits in an octet are rejected");
    CHECK(bad("ANYTHING"), "ANY must stand alone");
    CHECK(ftpd_adr_parse(NULL, NULL, NULL) == FTPD_ADR_BAD,
          "a NULL value is rejected");

    /* --- the config keeps the string, the socket layer the binary ----- */
    memset(out, '\0', sizeof(out));
    CHECK(ftpd_adr_parse("10,20,30,40", out, NULL) == FTPD_ADR_OK &&
          strcmp(out, "10.20.30.40") == 0,
          "the address alone can be requested");
    addr = 0;
    CHECK(ftpd_adr_parse("10.20.30.40", NULL, &addr) == FTPD_ADR_OK &&
          addr == QUAD(10, 20, 30, 40),
          "the binary alone can be requested");

    /* --- ftpd_adr_conflicts(): which leftover sockets the stale-port
    ** sweep may close (#109).  Getting this wrong either leaves the port
    ** occupied or closes a socket on an interface we never asked for. --- */
    CHECK(ftpd_adr_conflicts(0u, 0u), "ANY over ANY conflicts");
    CHECK(ftpd_adr_conflicts(QUAD(10, 1, 2, 3), QUAD(10, 1, 2, 3)),
          "the same address conflicts with itself");
    CHECK(ftpd_adr_conflicts(0u, QUAD(10, 1, 2, 3)),
          "a socket on ANY blocks a bind to one interface");
    CHECK(ftpd_adr_conflicts(QUAD(10, 1, 2, 3), 0u),
          "a socket on one interface blocks a bind to ANY");
    CHECK(!ftpd_adr_conflicts(QUAD(10, 1, 2, 3), QUAD(10, 1, 2, 4)),
          "two different interfaces do not conflict -- leave that one alone");
    CHECK(!ftpd_adr_conflicts(QUAD(127, 0, 0, 1), QUAD(192, 168, 0, 1)),
          "loopback does not block a bind to a real interface");

    return mbt_test_summary("TSTADR");
}
