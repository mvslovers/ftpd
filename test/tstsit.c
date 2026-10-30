/* TSTSIT.C
** Tests for ftpd_site_dispatch() -- SITE keywords and keyword values are
** case-insensitive (issue #189, RFC 959 section 5.3), keywords may be
** abbreviated to the z/OS minimum (#130), and SITE RETPD / DIRECTORY.
**
** For every spelling pair the test runs the upper-case form and the variant
** against a fresh session and requires the same reply and the same session
** state.  The upper-case run is also checked against the value it must
** produce, so two runs that agree on a wrong answer do not pass.
**
** The SITE verb itself is upper-cased by the session loop before dispatch
** and never reaches ftpd_site_dispatch(); that part is covered by the
** end-to-end script (test/test_ftpd.sh, section "SITE in any spelling").
**
** MVS only: ftpd.h pulls in the libc370 MVS headers.  The two reply
** functions and the volume check are replaced here, so no socket, no UCB
** scan and no FTPD log are needed.
*/
#include "ftpd.h"
#include "ftpd#ses.h"
#include "ftpd#sit.h"
#include "ftpd#mvs.h"
#include "ftpd#jes.h"
#include "ftpd#jwn.h"
#include <mbtcheck.h>

static ftpd_server_t    server;
static ftpd_session_t   sess;
static int              test_max_retpd = 9999;  /* MAXRETPD of fresh_session() */

static int  last_code;
static char last_text[256];
static int  replies;

int
ftpd_session_reply(ftpd_session_t *s, int code, const char *fmt, ...)
{
    va_list ap;

    (void)s;
    va_start(ap, fmt);
    vsnprintf(last_text, sizeof(last_text), fmt, ap);
    va_end(ap);
    last_code = code;
    replies++;
    return 0;
}

int
ftpd_session_reply_multi(ftpd_session_t *s, int code,
                         const char *first, const char *last)
{
    (void)s;
    snprintf(last_text, sizeof(last_text), "%s|%s", first, last);
    last_code = code;
    replies++;
    return 0;
}

/* Only an upper-case serial is "mounted": a value that reaches the check
** unnormalised is refused and shows up as a state difference. */
int
ftpdcfg_volume_online(const char *volser, unsigned short *dasdtype)
{
    if (dasdtype)
        *dasdtype = 0;
    return strcmp(volser, "PUB001") == 0 || strcmp(volser, "SYSRES") == 0;
}

/* The JESSPOOL probe behind SITE JESOWNER is replaced: the real one asks
** RACF.  The classification of the value is the real one (ftpd#jwn.c); only
** the answer to "may this session see that owner" is the test's. */
static int  owner_probe_ok = 1;
static int  owner_probes;

int
ftpd_jes_owner_ok(ftpd_session_t *s, const char *value, char *canon,
                  int canonsz)
{
    int kind = ftpd_jwn_narrows(s->user, value, canon, canonsz);

    if (kind == FTPD_JWN_NR_RESET || kind == FTPD_JWN_NR_OWN)
        return FTPD_JES_OWNER_OK;
    if (kind == FTPD_JWN_NR_ALL || kind == FTPD_JWN_NR_OTHER) {
        owner_probes++;
        if (owner_probe_ok)
            return FTPD_JES_OWNER_OK;
        canon[0] = '\0';
        return FTPD_JES_OWNER_DENIED;
    }
    canon[0] = '\0';
    return FTPD_JES_OWNER_BAD;
}

/* The model data set lookup is replaced: the real one needs a catalog, a
** VTOC and RAKF.  Names stand for the cases the real one can answer. */
static char model_asked[64];
static int  model_calls;

int
ftpd_mvs_dcbmodel(ftpd_session_t *s, const char *dsn, ftpd_dcbmodel_t *m,
                  char *err, int errsz)
{
    (void)s;
    strncpy(model_asked, dsn, sizeof(model_asked) - 1);
    model_calls++;
    memset(m, 0, sizeof(*m));

    if (strcmp(dsn, "MODEL.VB") == 0) {
        strcpy(m->recfm, "VB");
        m->lrecl = 255;
        m->blksize = 6233;
        return 0;
    }
    if (strcmp(dsn, "MODEL.SPAN") == 0) {
        strcpy(m->recfm, "VB");
        m->lrecl = 1000;
        m->blksize = 4000;
        strcpy(m->note, "RECFM attribute S of the model is not carried over");
        return 0;
    }
    if (strcmp(dsn, "NO.ACCESS") == 0)
        snprintf(err, errsz, "DCBDSN: access to %s denied", dsn);
    else
        snprintf(err, errsz, "DCBDSN: %s is not cataloged", dsn);
    return -1;
}

static void
fresh_session(void)
{
    memset(&server, 0, sizeof(server));
    memset(&sess, 0, sizeof(sess));
    sess.server = &server;
    strcpy(sess.user, "IBMUSER");
    strcpy(server.config.defaults.recfm, "FB");
    server.config.defaults.lrecl = 80;
    server.config.defaults.blksize = 3120;
    server.config.defaults.primary = 100;
    server.config.defaults.secondary = 50;
    strcpy(server.config.defaults.unit, "3380");
    strcpy(server.config.defaults.volume, "SYSRES");
    server.config.defaults.directory = 25;
    server.config.max_retpd = test_max_retpd;
    strcpy(sess.alloc.recfm, "FB");
    sess.alloc.lrecl = 80;
    sess.alloc.blksize = 3120;
    sess.alloc.primary = 100;
    sess.alloc.secondary = 50;
    sess.alloc.dirblks = 25;
    sess.alloc.retpd = 0;
    strcpy(sess.alloc.spacetype, "TRK");
    strcpy(sess.alloc.unit, "3380");
    strcpy(sess.alloc.volume, "SYSRES");
    sess.filetype = FT_SEQ;
    sess.fsmode = FS_MVS;
    sess.prev_fsmode = FS_MVS;
}

/* Everything SITE can change, in one comparable piece. */
typedef struct state {
    ftpd_alloc_t    alloc;
    int             filetype;
    int             fsmode;
    int             prev_fsmode;
    char            jes_owner[9];
    char            jes_jobname[9];
    char            jes_status[8];
    int             trailing;
    int             truncate;
} state_t;

static void
snapshot(state_t *st)
{
    memset(st, 0, sizeof(*st));
    st->alloc = sess.alloc;
    st->filetype = sess.filetype;
    st->fsmode = sess.fsmode;
    st->prev_fsmode = sess.prev_fsmode;
    strcpy(st->jes_owner, sess.jes_owner);
    strcpy(st->jes_jobname, sess.jes_jobname);
    strcpy(st->jes_status, sess.jes_status);
    st->trailing = sess.trailing;
    st->truncate = sess.truncate;
}

/* Member by member: a struct compare would also compare padding. */
static int
state_equal(const state_t *a, const state_t *b)
{
    return strcmp(a->alloc.recfm, b->alloc.recfm) == 0
        && a->alloc.lrecl == b->alloc.lrecl
        && a->alloc.blksize == b->alloc.blksize
        && a->alloc.primary == b->alloc.primary
        && a->alloc.secondary == b->alloc.secondary
        && strcmp(a->alloc.spacetype, b->alloc.spacetype) == 0
        && strcmp(a->alloc.volume, b->alloc.volume) == 0
        && strcmp(a->alloc.unit, b->alloc.unit) == 0
        && a->alloc.dirblks == b->alloc.dirblks
        && a->alloc.retpd == b->alloc.retpd
        && a->filetype == b->filetype
        && a->fsmode == b->fsmode
        && a->prev_fsmode == b->prev_fsmode
        && strcmp(a->jes_owner, b->jes_owner) == 0
        && strcmp(a->jes_jobname, b->jes_jobname) == 0
        && strcmp(a->jes_status, b->jes_status) == 0
        && a->trailing == b->trailing
        && a->truncate == b->truncate;
}

/* Run one SITE argument string on a fresh session. */
static void
run(const char *arg, state_t *st, int *code, char *text, int textsz)
{
    fresh_session();
    last_code = 0;
    last_text[0] = '\0';
    replies = 0;
    ftpd_site_dispatch(&sess, arg);
    snapshot(st);
    *code = (replies == 1) ? last_code : -replies;
    strncpy(text, last_text, textsz - 1);
    text[textsz - 1] = '\0';
}

/* The variant must behave exactly like the canonical spelling. */
static void
same(const char *canon, const char *variant)
{
    state_t a;
    state_t b;
    int ca, cb;
    char ta[256], tb[256];
    char msg[160];

    run(canon, &a, &ca, ta, sizeof(ta));
    run(variant, &b, &cb, tb, sizeof(tb));

    snprintf(msg, sizeof(msg), "\"%s\" replies like \"%s\" (%d)",
             variant, canon, ca);
    CHECK(ca == 200 && cb == ca && strcmp(ta, tb) == 0, msg);

    snprintf(msg, sizeof(msg), "\"%s\" leaves the same state as \"%s\"",
             variant, canon);
    CHECK(state_equal(&a, &b), msg);
}

/* The token is refused: "Unrecognized parameter" naming it, and nothing
** changed -- the state is the one a SITE that did nothing leaves. */
static void
retpd_bad(const char *value, const char *msg)
{
    state_t st;
    int code;
    char text[256];
    char arg[64];

    snprintf(arg, sizeof(arg), "RETPD=5 RETPD=%s", value);
    run(arg, &st, &code, text, sizeof(text));
    CHECK(st.alloc.retpd == 5 && strstr(text, "RETPD must be 0-9999") != NULL,
          msg);
}

static void
refused(const char *arg)
{
    state_t before;
    state_t after;
    int code;
    char text[256];
    char msg[160];

    run("", &before, &code, text, sizeof(text));
    run(arg, &after, &code, text, sizeof(text));

    snprintf(msg, sizeof(msg), "\"%s\" is unrecognised and names the token",
             arg);
    CHECK(code == 200 && strstr(text, "Unrecognized parameter") != NULL
          && strstr(text, arg) != NULL, msg);

    snprintf(msg, sizeof(msg), "\"%s\" changes nothing", arg);
    CHECK(state_equal(&before, &after), msg);
}

int
main(void)
{
    state_t st;
    int code;
    char text[256];

    printf("=== FTPD SITE case-insensitivity tests (#189) ===\n");

    /* --- what the canonical spelling does ----------------------------- */
    run("RECFM=VB", &st, &code, text, sizeof(text));
    CHECK(strcmp(st.alloc.recfm, "VB") == 0, "RECFM=VB sets VB");
    run("LRECL=255", &st, &code, text, sizeof(text));
    CHECK(st.alloc.lrecl == 255, "LRECL=255 sets 255");
    run("VOLUME=PUB001", &st, &code, text, sizeof(text));
    CHECK(strcmp(st.alloc.volume, "PUB001") == 0, "VOLUME=PUB001 sets PUB001");
    run("UNIT=3390", &st, &code, text, sizeof(text));
    CHECK(strcmp(st.alloc.unit, "3390") == 0, "UNIT=3390 sets 3390");
    run("FILETYPE=JES", &st, &code, text, sizeof(text));
    CHECK(st.filetype == FT_JES, "FILETYPE=JES switches to JES");
    run("JESOWNER=ABC", &st, &code, text, sizeof(text));
    CHECK(strcmp(st.jes_owner, "ABC") == 0, "JESOWNER=ABC sets ABC");
    run("JESJOBNAME=MYJOB", &st, &code, text, sizeof(text));
    CHECK(strcmp(st.jes_jobname, "MYJOB") == 0, "JESJOBNAME=MYJOB sets MYJOB");
    run("TRAILING", &st, &code, text, sizeof(text));
    CHECK(st.trailing == 1, "TRAILING toggles on");

    /* --- keywords ------------------------------------------------------ */
    same("RECFM=FB", "recfm=fb");
    same("RECFM=VB", "Recfm=Vb");
    same("LRECL=255", "lrecl=255");
    same("BLKSIZE=6240", "BlkSize=6240");
    same("PRIMARY=10", "primary=10");
    same("SECONDARY=5", "secondary=5");
    same("DIRECTORY=20", "directory=20");
    same("TRACKS", "tracks");
    same("CYLINDERS", "cylinders");
    same("BLOCKS", "blocks");
    same("VOLUME=PUB001", "volume=pub001");
    same("UNIT=3390", "unit=3390");
    same("FILETYPE=JES", "filetype=jes");
    same("FILETYPE=JES", "FileType=Jes");
    same("FILETYPE=SEQ", "filetype=seq");
    same("TRAILING", "trailing");
    same("TRUNCATE", "truncate");
    same("DATACLAS=X", "dataclas=x");
    same("STORCLAS=X", "storclas=x");
    same("MGMTCLAS=X", "mgmtclas=x");

    /* --- values that name something --------------------------------- */
    same("JESJOBNAME=MYJOB", "jesjobname=myjob");
    same("JESOWNER=IBMUSER", "jesowner=ibmuser");
    same("JESSTATUS=OUTPUT", "jesstatus=output");
    same("JESSTATUS=ALL", "JesStatus=All");

    /* --- several parameters in one command ---------------------------- */
    same("RECFM=VB LRECL=255 FILETYPE=JES JESOWNER=ABC",
         "recfm=vb lrecl=255 filetype=jes jesowner=abc");
    same("RECFM=VB LRECL=255 FILETYPE=JES JESOWNER=ABC",
         "Recfm=vb LRECL=255 fileType=JES JesOwner=abc");

    /* --- a parameter that is not recognised stays unrecognised -------- */
    run("JESINTERFACELEVEL=1", &st, &code, text, sizeof(text));
    CHECK(code == 200 && strstr(text, "Unrecognized parameter") != NULL,
          "JESINTERFACELEVEL=1 is unrecognised");
    run("jesinterfacelevel=1", &st, &code, text, sizeof(text));
    CHECK(code == 200 && strstr(text, "Unrecognized parameter") != NULL,
          "jesinterfacelevel=1 is unrecognised too");

    /* --- ISPFSTATS: the Zowe FTP plugin sends it with every transfer (#302) */
    run("FILETYPE=JES ISPFSTATS", &st, &code, text, sizeof(text));
    CHECK(code == 200 && strstr(text, "Unrecognized") == NULL,
          "ISPFSTATS is accepted without a warning");
    CHECK(st.filetype == FT_JES, "  and FILETYPE=JES next to it still applies");
    same("ISPFSTATS", "ispfstats");

    /* --- settings that took effect nowhere (#207) ---------------------- */

    /* TRUNCATE is read by STOR/APPE (test/test_ftpd.sh, section "TRUNCATE"):
    ** here only that it toggles, and toggles back. */
    run("TRUNCATE", &st, &code, text, sizeof(text));
    CHECK(code == 200 && st.truncate == 1, "TRUNCATE toggles on");
    run("TRUNCATE TRUNCATE", &st, &code, text, sizeof(text));
    CHECK(code == 200 && st.truncate == 0, "TRUNCATE twice toggles off again");

    /* RDW and a line end FTPD does not handle are refused with 501 and
    ** change nothing, instead of a 200 for a setting that does nothing. */
    run("RDW", &st, &code, text, sizeof(text));
    CHECK(code == 501 && strstr(text, "RDW") != NULL
          && strstr(text, "not supported") != NULL,
          "RDW is refused with 501");
    run("rdw", &st, &code, text, sizeof(text));
    CHECK(code == 501, "rdw is refused with 501 too");
    run("RDW TRUNCATE", &st, &code, text, sizeof(text));
    CHECK(code == 501 && st.truncate == 1 && strstr(text, "applied") != NULL,
          "RDW is refused, TRUNCATE on the same SITE is applied, and the "
          "reply says so");

    run("SBSENDEOL=CRLF", &st, &code, text, sizeof(text));
    CHECK(code == 200 && strcmp(text, "SITE command was accepted") == 0,
          "SBSENDEOL=CRLF is accepted: it is what FTPD does");
    run("sbsendeol=crlf", &st, &code, text, sizeof(text));
    CHECK(code == 200, "sbsendeol=crlf is accepted too");
    run("SBSENDEOL=LF", &st, &code, text, sizeof(text));
    CHECK(code == 501 && strstr(text, "SBSENDEOL=LF") != NULL
          && strstr(text, "CRLF") != NULL,
          "SBSENDEOL=LF is refused, naming the value");
    run("SBSENDEOL=CR", &st, &code, text, sizeof(text));
    CHECK(code == 501, "SBSENDEOL=CR is refused");
    run("SBSENDEOL=NL", &st, &code, text, sizeof(text));
    CHECK(code == 501, "SBSENDEOL=NL is refused");
    run("SBSENDEOL=LFCR", &st, &code, text, sizeof(text));
    CHECK(code == 501, "SBSENDEOL=LFCR is refused");
    run("SBSENDEOL", &st, &code, text, sizeof(text));
    CHECK(code == 200 && strcmp(text, "SITE command was accepted") == 0,
          "a bare SBSENDEOL returns to the default, which is CRLF");

    /* The value is the client's own: it must reach the reply verbatim, never
    ** be read as a format. */
    run("SBSENDEOL=%s%s%s%n", &st, &code, text, sizeof(text));
    CHECK(code == 501 && strstr(text, "SBSENDEOL=%s%s%s%n is not") != NULL,
          "conversion characters in a refused SBSENDEOL come back verbatim");
    run("SBSENDEOL=%s%s%s%n TRUNCATE", &st, &code, text, sizeof(text));
    CHECK(code == 501 && strstr(text, "%s%s%s%n") != NULL
          && strstr(text, "applied") != NULL,
          "the same with another parameter on the line");

    /* JESSTATUS is a filter now: only its four values are taken. */
    run("", &st, &code, text, sizeof(text));
    CHECK(st.jes_status[0] == '\0', "a fresh session filters nothing");
    run("JESSTATUS=OUTPUT", &st, &code, text, sizeof(text));
    CHECK(code == 200 && strcmp(st.jes_status, "OUTPUT") == 0,
          "JESSTATUS=OUTPUT sets OUTPUT");
    run("jesstatus=active", &st, &code, text, sizeof(text));
    CHECK(code == 200 && strcmp(st.jes_status, "ACTIVE") == 0,
          "jesstatus=active sets ACTIVE");
    run("JESSTATUS=INPUT", &st, &code, text, sizeof(text));
    CHECK(strcmp(st.jes_status, "INPUT") == 0, "JESSTATUS=INPUT sets INPUT");
    run("JESSTATUS=OUTPUT JESSTATUS=ALL", &st, &code, text, sizeof(text));
    CHECK(strcmp(st.jes_status, "ALL") == 0, "JESSTATUS=ALL resets the filter");
    run("JESSTATUS=OUTPUT JESSTATUS", &st, &code, text, sizeof(text));
    CHECK(code == 200 && strcmp(st.jes_status, "ALL") == 0,
          "a bare JESSTATUS goes back to ALL");
    run("JESSTATUS=OUTPUT JESSTATUS=HELD", &st, &code, text, sizeof(text));
    CHECK(code == 200 && strcmp(st.jes_status, "OUTPUT") == 0
          && strstr(text, "JESSTATUS must be ALL, INPUT, ACTIVE or OUTPUT")
             != NULL,
          "JESSTATUS=HELD is refused with a warning, OUTPUT stays");
    run("JESSTATUS=OUT", &st, &code, text, sizeof(text));
    CHECK(st.jes_status[0] == '\0' && strstr(text, "JESSTATUS must be") != NULL,
          "an abbreviated JESSTATUS value is refused");

    /* --- minimum abbreviations, as z/OS marks them (#130) -------------- */
    same("PRIMARY=100", "PRI=100");
    same("PRIMARY=100", "PRIM=100");
    same("PRIMARY=100", "pri=100");
    same("SECONDARY=7", "SEC=7");
    same("TRACKS", "TR");
    same("TRACKS", "TRA");
    same("CYLINDERS", "CY");
    same("CYLINDERS", "CYL");
    same("BLOCKS", "BL");
    same("BLOCKS", "BLO");
    same("RECFM=VB", "REC=VB");
    same("RECFM=VB", "RECF=VB");
    same("LRECL=255", "LR=255");
    same("LRECL=255", "LREC=255");
    same("BLKSIZE=6240", "BLK=6240");
    same("BLKSIZE=6240", "BLKS=6240");
    same("BLKSIZE=6240", "BLOCKSIZE=6240");
    same("BLKSIZE=6240", "BLOCKSI=6240");
    same("DIRECTORY=20", "DIR=20");
    same("UNIT=3390", "UNI=3390");
    same("DIRECTORY=20", "D=20");
    same("DIRECTORY=20", "DI=20");
    same("UNIT=3390", "U=3390");
    same("UNIT=3390", "UN=3390");
    same("VOLUME=PUB001", "VOL=PUB001");
    same("FILETYPE=JES", "FILE=JES");
    same("TRAILING", "TRAIL");
    same("TRAILING", "TRAILINGBLANKS");
    same("TRUNCATE", "TRUN");
    same("JESJOBNAME=MYJOB", "JESJOBN=MYJOB");
    same("JESOWNER=ABC", "JESO=ABC");
    same("JESSTATUS=ALL", "JESST=ALL");
    same("DATACLAS=X", "DATAC=X");
    same("DATACLAS=X", "DATACLASS=X");
    same("STORCLAS=X", "STO=X");
    same("MGMTCLAS=X", "MG=X");
    same("RETPD=30", "RET=30");
    same("RETPD=30", "ret=30");
    same("RECFM=VB LRECL=255 PRIMARY=9", "REC=VB LR=255 PRI=9");

    run("PRI=100", &st, &code, text, sizeof(text));
    CHECK(st.alloc.primary == 100, "PRI=100 sets the primary quantity");
    run("TR", &st, &code, text, sizeof(text));
    CHECK(strcmp(st.alloc.spacetype, "TRK") == 0, "TR keeps tracks");
    run("CY", &st, &code, text, sizeof(text));
    CHECK(strcmp(st.alloc.spacetype, "CYL") == 0, "CY selects cylinders");
    run("BLOCKSIZE=6240", &st, &code, text, sizeof(text));
    CHECK(st.alloc.blksize == 6240, "BLOCKSIZE=6240 sets the block size");

    /* Below the minimum, past the end, or in nobody's keyword: refused,
    ** and nothing changes.  "T" and "S" would each name several keywords. */
    refused("PR=100");
    refused("P=100");
    refused("SE=5");
    refused("S=5");
    refused("T");
    refused("TRAI");
    refused("TRU");
    refused("B=1");
    refused("BLOCKSIZEE=1");
    refused("R=VB");
    refused("RE=VB");
    refused("L=80");
    refused("JES=1");
    refused("JESJ=X");
    refused("J=X");
    refused("PRIMARYY=100");
    refused("BLKSIZEE=1");
    refused("DCBDSNX=A.B");
    refused("DSNTYPE=LARGE");
    refused("DIRECTORYMODE=X");

    /* --- RETPD (#130) ------------------------------------------------ */
    run("", &st, &code, text, sizeof(text));
    CHECK(st.alloc.retpd == 0, "a fresh session has no retention period");
    run("RETPD=30", &st, &code, text, sizeof(text));
    CHECK(code == 200 && strcmp(text, "SITE command was accepted") == 0
          && st.alloc.retpd == 30, "RETPD=30 sets 30");
    run("RETPD=0", &st, &code, text, sizeof(text));
    CHECK(st.alloc.retpd == 0 && strstr(text, "|") == NULL, "RETPD=0 is valid");
    run("RETPD=9999", &st, &code, text, sizeof(text));
    CHECK(st.alloc.retpd == 9999 && strstr(text, "|") == NULL,
          "RETPD=9999 is the top of the range");
    retpd_bad("10000", "RETPD=10000 is refused with a warning, 5 stays");
    retpd_bad("abc", "RETPD=abc is refused, 5 stays");
    retpd_bad("-1", "RETPD=-1 is refused, 5 stays");
    retpd_bad("99999999999999", "an absurd RETPD is refused, 5 stays");
    run("RETPD=5 RETPD", &st, &code, text, sizeof(text));
    CHECK(st.alloc.retpd == 0, "a bare RETPD goes back to no retention");

    /* --- MAXRETPD: the server's ceiling on SITE RETPD (#201) ----------- */
    test_max_retpd = 30;
    run("RETPD=30", &st, &code, text, sizeof(text));
    CHECK(code == 200 && st.alloc.retpd == 30,
          "RETPD equal to MAXRETPD is accepted");
    run("RETPD=29", &st, &code, text, sizeof(text));
    CHECK(code == 200 && st.alloc.retpd == 29,
          "RETPD below MAXRETPD is accepted");
    run("RETPD=5 RETPD=31", &st, &code, text, sizeof(text));
    CHECK(code == 501 && st.alloc.retpd == 5
          && strstr(text, "31") != NULL && strstr(text, "30 days") != NULL,
          "RETPD one over MAXRETPD is 501, names the limit, 5 stays");
    run("RETPD=9999", &st, &code, text, sizeof(text));
    CHECK(code == 501 && st.alloc.retpd == 0,
          "RETPD=9999 over a limit of 30 is refused, still no retention");
    run("RETPD=31 RECFM=VB", &st, &code, text, sizeof(text));
    CHECK(code == 501 && st.alloc.retpd == 0
          && strcmp(st.alloc.recfm, "VB") == 0
          && strstr(text, "applied") != NULL,
          "another parameter on the same SITE is applied, and the reply says so");
    run("RETPD=10000", &st, &code, text, sizeof(text));
    CHECK(code == 200 && st.alloc.retpd == 0
          && strstr(text, "RETPD must be 0-9999") != NULL,
          "a value outside 0-9999 keeps its own warning");
    run("RETPD", &st, &code, text, sizeof(text));
    CHECK(code == 200 && st.alloc.retpd == 0, "a bare RETPD is never refused");

    test_max_retpd = 0;
    run("RETPD=0", &st, &code, text, sizeof(text));
    CHECK(code == 200 && st.alloc.retpd == 0,
          "MAXRETPD=0: RETPD=0 is accepted");
    run("RETPD=1", &st, &code, text, sizeof(text));
    CHECK(code == 501 && st.alloc.retpd == 0 && strstr(text, "0 days") != NULL,
          "MAXRETPD=0: any retention is refused");

    test_max_retpd = 9999;
    run("RETPD=9999", &st, &code, text, sizeof(text));
    CHECK(code == 200 && st.alloc.retpd == 9999,
          "MAXRETPD=9999 never refuses a valid value");

    /* --- DIRECTORY and DEFDIRECTORY (#130) --------------------------- */
    run("", &st, &code, text, sizeof(text));
    CHECK(st.alloc.dirblks == 25, "a session starts with DEFDIRECTORY");
    run("DIRECTORY=40", &st, &code, text, sizeof(text));
    CHECK(st.alloc.dirblks == 40, "SITE DIRECTORY=40 overrides it");
    run("DIRECTORY=40 DIRECTORY", &st, &code, text, sizeof(text));
    CHECK(st.alloc.dirblks == 25, "a bare DIRECTORY goes back to DEFDIRECTORY");
    run("DIRECTORY=0", &st, &code, text, sizeof(text));
    CHECK(st.alloc.dirblks == 25 && strstr(text, "DIRECTORY must be") != NULL,
          "DIRECTORY=0 is refused, DEFDIRECTORY stays");

    /* --- DCBDSN: RECFM/LRECL/BLKSIZE from a model data set (#180) ------ */
    model_calls = 0;
    run("DCBDSN=MODEL.VB", &st, &code, text, sizeof(text));
    CHECK(code == 200 && strcmp(text, "SITE command was accepted") == 0
          && strcmp(st.alloc.recfm, "VB") == 0 && st.alloc.lrecl == 255
          && st.alloc.blksize == 6233 && strcmp(model_asked, "MODEL.VB") == 0,
          "DCBDSN=MODEL.VB takes RECFM, LRECL and BLKSIZE of the model");
    run("DCBDSN='model.vb'", &st, &code, text, sizeof(text));
    CHECK(code == 200 && strcmp(model_asked, "MODEL.VB") == 0
          && strcmp(st.alloc.recfm, "VB") == 0,
          "quotes are dropped and the name is upper-cased");
    same("DCBDSN=MODEL.VB", "dcbdsn=model.vb");
    same("DCBDSN=MODEL.VB", "DCB=MODEL.VB");
    same("DCBDSN=MODEL.VB", "dc=MODEL.VB");

    run("DCBDSN=MODEL.VB LRECL=100", &st, &code, text, sizeof(text));
    CHECK(strcmp(st.alloc.recfm, "VB") == 0 && st.alloc.lrecl == 100
          && st.alloc.blksize == 6233,
          "LRECL after DCBDSN wins, the rest comes from the model");
    run("LRECL=100 DCBDSN=MODEL.VB", &st, &code, text, sizeof(text));
    CHECK(st.alloc.lrecl == 255,
          "DCBDSN after LRECL wins: the last one given counts");
    run("RECFM=U DCBDSN", &st, &code, text, sizeof(text));
    CHECK(code == 200 && strcmp(st.alloc.recfm, "FB") == 0
          && st.alloc.lrecl == 80 && st.alloc.blksize == 3120,
          "a bare DCBDSN goes back to DEFRECFM, DEFLRECL, DEFBLKSIZE");

    run("DCBDSN=MODEL.SPAN", &st, &code, text, sizeof(text));
    CHECK(code == 200 && strcmp(st.alloc.recfm, "VB") == 0
          && st.alloc.lrecl == 1000
          && strstr(text, "attribute S") != NULL
          && strstr(text, "SITE command was accepted") != NULL,
          "attributes FTPD cannot carry over are named in a warning");

    run("RECFM=VB DCBDSN=NO.SUCH", &st, &code, text, sizeof(text));
    CHECK(code == 501 && strstr(text, "NO.SUCH is not cataloged") != NULL
          && strstr(text, "applied") != NULL
          && strcmp(st.alloc.recfm, "VB") == 0 && st.alloc.lrecl == 80,
          "a model that is not cataloged is 501 and changes nothing");
    run("DCBDSN=NO.SUCH", &st, &code, text, sizeof(text));
    CHECK(code == 501 && strcmp(st.alloc.recfm, "FB") == 0
          && st.alloc.lrecl == 80 && st.alloc.blksize == 3120,
          "a refused DCBDSN keeps the session's values");
    run("DCBDSN=NO.ACCESS", &st, &code, text, sizeof(text));
    CHECK(code == 501 && strstr(text, "denied") != NULL
          && strcmp(st.alloc.recfm, "FB") == 0,
          "a model the user may not read is refused");

    model_calls = 0;
    run("DCBDSN=A..B", &st, &code, text, sizeof(text));
    CHECK(code == 501 && model_calls == 0, "an invalid name is refused unread");
    run("DCBDSN=MODEL.*", &st, &code, text, sizeof(text));
    CHECK(code == 501 && model_calls == 0, "a pattern is refused unread");
    run("DCBDSN=MODEL.PDS(MEM)", &st, &code, text, sizeof(text));
    CHECK(code == 501 && model_calls == 0, "a member is refused unread");

    /* --- JESOWNER narrows, never widens (#90) ----------------------- */
    run("JESOWNER=ibmuser", &st, &code, text, sizeof(text));
    CHECK(code == 200 && strcmp(st.jes_owner, "IBMUSER") == 0,
          "JESOWNER=<own userid> is stored upper case");
    run("JESOWNER=", &st, &code, text, sizeof(text));
    CHECK(code == 200 && st.jes_owner[0] == '\0',
          "an empty JESOWNER resets the filter");
    run("JESOWNER=*", &st, &code, text, sizeof(text));
    CHECK(code == 200 && strcmp(st.jes_owner, "*") == 0,
          "JESOWNER=* is stored when the probe passes");

    owner_probes = 0;
    run("JESOWNER=ABC*", &st, &code, text, sizeof(text));
    CHECK(code == 501 && strcmp(text, "JESOWNER is not permitted") == 0
          && st.jes_owner[0] == '\0' && owner_probes == 0,
          "JESOWNER=ABC* is 501 with the fixed text, nothing stored");
    run("JESO=ABC*", &st, &code, text, sizeof(text));
    CHECK(code == 501 && st.jes_owner[0] == '\0',
          "the abbreviation JESO=ABC* is 501 as well");
    run("JESOWNER=ABCDEFGHI", &st, &code, text, sizeof(text));
    CHECK(code == 501 && st.jes_owner[0] == '\0' && owner_probes == 0,
          "nine characters are 501, not cut to eight");
    run("JESOWNER=A.B", &st, &code, text, sizeof(text));
    CHECK(code == 501 && st.jes_owner[0] == '\0',
          "a character outside A-Z 0-9 $ # @ is 501");

    owner_probe_ok = 0;
    st.jes_owner[0] = '\0';
    run("JESOWNER=ABC", &st, &code, text, sizeof(text));
    CHECK(code == 200 && st.jes_owner[0] == '\0'
          && strstr(text, "User IBMUSER is not authorized to filter on "
                          "JESOWNER, JESOWNER remains IBMUSER") != NULL,
          "a foreign userid without READ is 200, ignored, names the filter");
    run("JESOWNER=*", &st, &code, text, sizeof(text));
    CHECK(code == 200 && st.jes_owner[0] == '\0'
          && strstr(text, "JESOWNER remains IBMUSER") != NULL,
          "* without READ on the universal profile is 200 and ignored");
    run("jesowner=abc", &st, &code, text, sizeof(text));
    CHECK(code == 200 && st.jes_owner[0] == '\0',
          "a denied value in lower case is ignored the same way");
    run("FILETYPE=JES JESOWNER=* JESJOBNAME=ABC*", &st, &code, text,
        sizeof(text));
    CHECK(code == 200 && strcmp(st.jes_jobname, "ABC*") == 0
          && st.jes_owner[0] == '\0',
          "the other parameters of the line are applied");
    run("JESOWNER=ABC*", &st, &code, text, sizeof(text));
    CHECK(code == 501 && strcmp(text, "JESOWNER is not permitted") == 0
          && st.jes_owner[0] == '\0',
          "a pattern stays 501 without READ");
    run("JESOWNER=ABCDEFGHI", &st, &code, text, sizeof(text));
    CHECK(code == 501 && st.jes_owner[0] == '\0',
          "nine characters stay 501 without READ");
    run("JESOWNER=IBMUSER", &st, &code, text, sizeof(text));
    CHECK(code == 200 && strcmp(st.jes_owner, "IBMUSER") == 0,
          "the own userid needs no right");
    /* A value that was accepted earlier stays when a later one is denied. */
    fresh_session();
    strcpy(sess.jes_owner, "ABC");
    last_text[0] = '\0';
    replies = 0;
    ftpd_site_dispatch(&sess, "JESOWNER=*");
    CHECK(replies == 1 && last_code == 200 && strcmp(sess.jes_owner, "ABC") == 0
          && strstr(last_text, "JESOWNER remains ABC") != NULL,
          "a denied value leaves the filter that was set and names it");
    run("JESOWNER=", &st, &code, text, sizeof(text));
    CHECK(code == 200 && st.jes_owner[0] == '\0',
          "a reset needs no right");
    owner_probe_ok = 1;

    return mbt_test_summary("TSTSIT");
}
