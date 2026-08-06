#ifndef FTPD_ADR_H
#define FTPD_ADR_H
/*
** FTPD Address Parameter Parsing
**
** One parser for every address FTPD is configured with (SRVBIND, PASVBIND,
** PASVADR).  They used to be read three different ways, with three different
** ideas of what an unreadable value means -- see issue #76.
**
** Free of project and MVS headers on purpose: this is the one piece of the
** configuration that a host unit test can link (test/tstadr.c).
*/

/* Buffer size for a stored address: "255.255.255.255" + NUL. */
#define FTPD_ADR_SIZE   16

/* ftpd_adr_parse() return codes */
#define FTPD_ADR_BAD    (-1)        /* not an address FTPD understands   */
#define FTPD_ADR_ANY    0           /* the literal ANY                   */
#define FTPD_ADR_OK     1           /* a valid IPv4 address              */

/*
** Parse a configured address.
**
** Accepted: "ANY" in any case, or an IPv4 address written either dotted
** (127.0.0.1) or comma separated (127,0,0,1) -- the comma form is what the
** FTP protocol itself uses, and PARMLIB members have always been written
** both ways.  All four octets must be present, in range 0-255, with nothing
** trailing them.
**
** out   -- optional, FTPD_ADR_SIZE bytes.  Receives "ANY" or the address in
**          dotted form.  Left untouched when the value is rejected, so a
**          caller can keep its default simply by ignoring the return code
**          (though every caller should say what default it fell back to).
** addr  -- optional.  Receives the address in host byte order; 0 for ANY.
**
** Returns FTPD_ADR_ANY, FTPD_ADR_OK or FTPD_ADR_BAD.  ANY and BAD are
** deliberately distinct: binding every address is a decision, not an error
** recovery.
*/
int ftpd_adr_parse(const char *value, char *out, unsigned *addr)
                                                            asm("FTPADRPA");

#endif /* FTPD_ADR_H */
