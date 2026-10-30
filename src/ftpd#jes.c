/*
** FTPD JES Interface — Job Submission + Job Query
**
** Job submission via internal reader (jesiropn/jesirput/jesircls).
** Job listing via checkpoint (jesopen/jesjob/jesclose).
**
** Line-by-line processing (no bulk buffer):
** 1. Read byte-by-byte from data connection in ASCII
** 2. Split at ASCII LF (0x0A) — each line is one JCL card
** 3. Convert each line ASCII→EBCDIC (CP037), pad to 80 with blanks
** 4. Hold back the JOB statement until its end is known, then inject
**    USER= and PASSWORD= continuation lines behind it (ftpd#jcd.c)
** 5. jesirput() per card, jesircl2() to trigger JES2 processing
**
** USER/PASSWORD injection follows mvsmf/src/jobsapi.c process_jobcard().
*/
#include "ftpd.h"
#include "ftpd#jcd.h"
#include "ftpd#jfl.h"
#include "ftpd#jid.h"
#include "ftpd#jsl.h"
#include "ftpd#ses.h"
#include "ftpd#aut.h"
#include "ftpd#dat.h"
#include "ftpd#jes.h"
#include "ftpd#xlt.h"
#include <mvs/jes2.h>
#include <ibm/jes2/jqe.h>

/* ASCII constants (wire format before translation) */
#define ASCII_LF    0x0A
#define ASCII_CR    0x0D

/* --------------------------------------------------------------------
** Helper: build an 80-byte EBCDIC card from an ASCII line.
** Pads with ASCII blanks (0x20) to 80, then translates to EBCDIC.
** ----------------------------------------------------------------- */
static void
build_card(ftpd_session_t *sess, const char *line, int linelen, char *card)
{
    if (linelen > 80) linelen = 80;
    /* Pad with ASCII space (0x20), NOT C literal ' ' which is EBCDIC 0x40.
    ** The entire card will be converted a2e, so padding must be ASCII. */
    memset(card, 0x20, 80);
    if (linelen > 0)
        memcpy(card, line, linelen);
    card[80] = '\0';

    /* Translate the full 80-byte card ASCII→EBCDIC (CP037) */
    if (sess->type == XFER_TYPE_A)
        ftpd_xlat_mvs_a2e((unsigned char *)card, 80);
}

/* --------------------------------------------------------------------
** Helper: submit one card to the internal reader.
** Returns jesirput() return code.
** ----------------------------------------------------------------- */
static int
submit_card(VSFILE *intrdr, char *card, int cardnum)
{
    /* cardnum fed a debug trace that was removed (4c9569e) -- it would
    ** have echoed the PASSWORD= card to the console. Kept for callers. */
    (void)cardnum;
    return jesirput(intrdr, card);
}

/* --------------------------------------------------------------------
** Helper: inject continuation cards after JOB card.
** - NOTIFY=DUMMY (only if JOB card has no NOTIFY)
** - USER=username
** - PASSWORD=password
** All cards built from C literals → already EBCDIC, no a2e conversion.
** ----------------------------------------------------------------- */
static int
inject_job_params(ftpd_session_t *sess, VSFILE *intrdr, int *cardcount,
                  int has_notify)
{
    char card[81];
    int rc;

    /* NOTIFY=DUMMY — suppress TSO notification for STC-submitted jobs */
    if (!has_notify) {
        memset(card, 0x40, 80);
        snprintf(card, 72, "//         NOTIFY=DUMMY,");
        card[strlen(card)] = 0x40;
        card[80] = '\0';

        rc = submit_card(intrdr, card, *cardcount + 1);
        if (rc < 0) return rc;
        (*cardcount)++;
    }

    /* USER= continuation card.
    ** Built from C string literals → already EBCDIC (c2asm370).
    ** sess->user is EBCDIC (stored uppercase at login).
    ** Do NOT run through ftpd_xlat_mvs_a2e — would double-convert. */
    memset(card, 0x40, 80);
    snprintf(card, 72, "//         USER=%s,", sess->user);
    card[strlen(card)] = 0x40;
    card[80] = '\0';

    rc = submit_card(intrdr, card, *cardcount + 1);
    if (rc < 0) return rc;
    (*cardcount)++;

    /* PASSWORD= continuation card — same: already EBCDIC, no conversion */
    memset(card, 0x40, 80);
    snprintf(card, 72, "//         PASSWORD=%s", sess->pass);
    card[strlen(card)] = 0x40;
    card[80] = '\0';

    rc = submit_card(intrdr, card, *cardcount + 1);
    if (rc < 0) return rc;
    (*cardcount)++;

    return 0;
}

/* The JOB statement is kept here until its last card is known: the cards
** put to the internal reader cannot be taken back, and a job without the
** credentials behind its JOB statement must not be started (#383). */
typedef struct jes_stream {
    VSFILE      *intrdr;
    ftpd_jcd_t  jcd;
    int         cardcount;
    char        held[FTPD_JCD_MAXCARDS][81];
} jes_stream_t;

#define JES_REFUSED 1

/* --------------------------------------------------------------------
** Helper: take one card of the stream.
** Returns 0 when it is done with, < 0 when jesirput() failed and
** JES_REFUSED when the JOB statement cannot take the credentials; nothing of
** the statement has been put to the reader then.
** ----------------------------------------------------------------- */
static int
jes_card(ftpd_session_t *sess, jes_stream_t *js, char *card)
{
    int rc;
    int i;

    switch (ftpd_jcd_feed(&js->jcd, card, 80)) {
    case FTPD_JCD_PASS:
        rc = submit_card(js->intrdr, card, js->cardcount + 1);
        if (rc < 0) return rc;
        js->cardcount++;
        return 0;

    case FTPD_JCD_HOLD:
        memcpy(js->held[js->jcd.nheld - 1], card, 80);
        js->held[js->jcd.nheld - 1][80] = '\0';
        return 0;

    case FTPD_JCD_DONE:
        memcpy(js->held[js->jcd.nheld - 1], card, 80);
        js->held[js->jcd.nheld - 1][80] = '\0';
        for (i = 0; i < js->jcd.nheld; i++) {
            rc = submit_card(js->intrdr, js->held[i], js->cardcount + 1);
            if (rc < 0) return rc;
            js->cardcount++;
        }
        return inject_job_params(sess, js->intrdr, &js->cardcount,
                                 js->jcd.has_notify);

    default:
        return JES_REFUSED;
    }
}

/* --------------------------------------------------------------------
** Submit JCL from data connection to JES2 internal reader.
**
** Called when sess->filetype == FT_JES and command is STOR.
** Reads JCL line-by-line (byte-by-byte from data connection),
** splits at LF, converts each line to an 80-byte EBCDIC card.
** ----------------------------------------------------------------- */
int
ftpd_jes_submit(ftpd_session_t *sess)
{
    VSFILE *intrdr = NULL;
    char line[81];          /* ASCII line being accumulated */
    char card[81];          /* EBCDIC card for jesirput */
    char jobid[9];
    jes_stream_t js;        /* about 1.3K: the JOB statement is held in it */
    int linepos;
    int rc;
    long total;
    char c;

    /* z/OS: "125 Sending Job to JES internal reader FIXrecfm 80" */
    ftpd_session_reply(sess, FTP_125,
        "Sending Job to JES internal reader FIXrecfm 80");

    if (ftpd_data_open(sess) != 0) {
        ftpd_session_reply(sess, FTP_425,
                           "Cannot open data connection");
        return 0;
    }

    /* Open JES2 internal reader under user's security environment */
    ftpd_acee_enter(sess);
    rc = jesiropn(&intrdr);
    ftpd_acee_leave(sess);
    if (rc < 0) {
        ftpd_log(LOG_ERROR, "JES: jesiropn() failed rc=%d", rc);
        ftpd_data_close(sess);
        ftpd_session_reply(sess, FTP_451,
                           "Unable to open JES internal reader");
        return 0;
    }

    ftpd_log(LOG_INFO, "JES: internal reader opened, receiving JCL");

    js.intrdr = intrdr;
    js.cardcount = 0;
    ftpd_jcd_init(&js.jcd);
    linepos = 0;
    total = 0;

    /* Read byte-by-byte, split at ASCII LF (0x0A) */
    while (ftpd_data_recv(sess, &c, 1) == 1) {
        total++;

        if (c == ASCII_LF) {
            /* End of line — strip trailing CR if present */
            if (linepos > 0 && line[linepos - 1] == ASCII_CR)
                linepos--;

            /* Build 80-byte EBCDIC card from this line */
            build_card(sess, line, linepos, card);

            /* JOB statement tracking + USER/PASSWORD injection */
            rc = jes_card(sess, &js, card);
            if (rc < 0) goto fail;
            if (rc == JES_REFUSED) goto refused;

            linepos = 0;
        } else {
            /* Accumulate byte (max 80 chars per line) */
            if (linepos < 80)
                line[linepos++] = c;
        }
    }

    /* Handle final line without trailing LF */
    if (linepos > 0) {
        build_card(sess, line, linepos, card);
        rc = jes_card(sess, &js, card);
        if (rc < 0) goto fail;
        if (rc == JES_REFUSED) goto refused;
    }

    /* The stream ended inside the JOB statement: what is held is no job. */
    if (ftpd_jcd_unfinished(&js.jcd)) goto refused;

    ftpd_data_close(sess);

    ftpd_log(LOG_INFO, "JES: closing internal reader, %d cards, %ld bytes",
             js.cardcount, total);

    /* Close internal reader — triggers JES2 processing.  jesircl2() hands
    ** the jobid out between the ENDREQ and the close; the old read of
    ** intrdr->rpl.rplrbar after jesircls() fetched it from the freed
    ** VSFILE (#102).  The handle is gone either way, so drop the pointer
    ** before checking the return code. */
    {
        unsigned char jobid_raw[8];

        rc = jesircl2(intrdr, jobid_raw);
        intrdr = NULL;
        if (rc < 0) {
            ftpd_log(LOG_ERROR, "JES: jesircl2() failed rc=%d", rc);
            ftpd_session_reply(sess, FTP_451,
                               "Failed to close internal reader");
            return 0;
        }

        memset(jobid, 0, sizeof(jobid));
        memcpy(jobid, jobid_raw, 8);
    }

    ftpd_log(LOG_INFO, "JES: job submitted: %s (%d cards, %ld bytes)",
             jobid, js.cardcount, total);

    /* z/OS FTP multi-line reply:
    ** 250-It is known to JES as JOBnnnnn
    ** 250 Transfer completed successfully. */
    {
        char first[64];
        snprintf(first, sizeof(first),
                 "It is known to JES as %s",
                 jobid[0] ? jobid : "(unknown)");
        ftpd_session_reply_multi(sess, FTP_250, first,
            "Transfer completed successfully.");
    }

    sess->xfer_count++;

    return 0;

fail:
    ftpd_data_close(sess);
    if (intrdr) {
        ftpd_log(LOG_ERROR, "JES: emergency closing internal reader");
        jesircls(intrdr);
    }
    ftpd_session_reply(sess, FTP_451,
                       "Failed to submit JCL to internal reader");
    return 0;

refused:
    /* The JOB statement of the stream cannot carry USER and PASSWORD: the
    ** cards that would follow it are not part of it, so the job would run
    ** without an owner and PASSWORD= would stand in its JCL listing.  Of the
    ** statement nothing was put; closing the reader submits only what came
    ** before it. */
    ftpd_data_close(sess);
    ftpd_log(LOG_ERROR, "JES: JOB statement cannot take the credentials, "
             "job not submitted");
    rc = jesircls(intrdr);
    if (rc < 0)
        ftpd_log(LOG_ERROR, "JES: jesircls() failed rc=%d", rc);
    ftpd_session_reply(sess, FTP_550,
                       "JOB statement cannot be continued with the "
                       "credentials; job not submitted");
    return 0;
}

/* ====================================================================
** JES Job Query — LIST in JES mode
** ==================================================================== */

/* --------------------------------------------------------------------
** Helper: map q_type to status string.
** Follows mvsmf jobsapi.c process_job() status mapping.
** ----------------------------------------------------------------- */
static const char *
job_status(unsigned char q_type)
{
    if (q_type & _XEQ)                  return "ACTIVE";
    if (q_type & _INPUT)                return "INPUT";
    if (q_type & _XMIT)                return "XMIT";
    if (q_type & _SETUP)                return "SETUP";
    if (q_type & _RECEIVE)              return "RECEIVE";
    if (q_type & (_OUTPUT | _HARDCPY))  return "OUTPUT";
    return "UNKNOWN";
}

/* --------------------------------------------------------------------
** The JESSTATUS the session works with, as it is shown in the replies.
** Empty means no SITE JESSTATUS was given: ALL.
** ----------------------------------------------------------------- */
static const char *
status_filter(const ftpd_session_t *sess)
{
    return sess->jes_status[0] ? sess->jes_status : "ALL";
}

/* --------------------------------------------------------------------
** Does the job pass the session's JESSTATUS filter (#207)?
** ----------------------------------------------------------------- */
static int
job_matches_status(const ftpd_session_t *sess, const JESJOB *job)
{
    return ftpd_jfl_match(sess->jes_status, job_status(job->q_type));
}

/* --------------------------------------------------------------------
** Helper: format return code from JCTCNVRC completion field.
** Follows mvsmf jobsapi.c process_job() retcode decoding.
** ----------------------------------------------------------------- */
static const char *
job_retcode(JESJOB *job, char *buf, int bufsz)
{
    unsigned int comp;

    if (!(job->q_type & (_OUTPUT | _HARDCPY)))
        return "";

    comp = job->completion;
    if ((comp >> 24) == 0x77) {
        /* Job executed — decode completion info */
        unsigned int abend = (comp >> 12) & 0xFFF;
        unsigned int maxcc =  comp        & 0xFFF;
        if (abend) {
            snprintf(buf, bufsz, "ABEND S%03X", abend);
        } else if ((job->jtflg & JESJOB_ABD) && maxcc) {
            snprintf(buf, bufsz, "ABEND U%04d", maxcc);
        } else {
            snprintf(buf, bufsz, "RC=%04d", maxcc);
        }
        return buf;
    }
    if (comp == 4 || comp == 8 || comp == 36)
        return "JCL ERROR";

    return "";
}

/* --------------------------------------------------------------------
** The job line of the JES listings: LIST of the jobs and LIST JOBnnnnn
** show the same line for a job (no line end).
**
** z/OS format:
**   JOBNAME  JOBID    OWNER    STATUS CLASS
**   TESTJOB  JOB00042 IBMUSER  OUTPUT A        RC=0000
** ----------------------------------------------------------------- */
static void
job_line(JESJOB *job, char *buf, int bufsz)
{
    char rcbuf[16];
    char class_buf[FTPD_JWN_PRTSZ];
    char jname[FTPD_JWN_PRTSZ];
    char jid[FTPD_JWN_PRTSZ];
    char jown[FTPD_JWN_PRTSZ];
    const char *class_str;

    ftpd_jwn_printable(jname, sizeof(jname), (const char *)job->jobname);
    ftpd_jwn_printable(jid, sizeof(jid), (const char *)job->jobid);
    ftpd_jwn_printable(jown, sizeof(jown), (const char *)job->owner);

    /* z/OS CLASS column: for STCs/TSO use first 3 chars of jobid,
    ** for normal jobs use the execution class character */
    if (strncmp(jid, "STC", 3) == 0) {
        class_str = "STC";
    } else if (strncmp(jid, "TSU", 3) == 0) {
        class_str = "TSU";
    } else if (job->eclass && job->eclass != ' ') {
        char ec[2];

        ec[0] = (char)job->eclass;
        ec[1] = '\0';
        ftpd_jwn_printable(class_buf, sizeof(class_buf), ec);
        class_str = class_buf;
    } else {
        class_str = "";
    }

    snprintf(buf, bufsz, "%-8.8s %-8.8s %-8.8s %-6s  %-3s %s",
             jname, jid, jown, job_status(job->q_type), class_str,
             job_retcode(job, rcbuf, sizeof(rcbuf)));
}

/* --------------------------------------------------------------------
** Is this DD one of the spool files the JES view numbers?  The number after
** the point in RETR JOBnnnnn.n and the ID column of LIST JOBnnnnn count
** these, from 1, in the order of the job's DD list.  The input of the job
** (JESJCLIN, JESINTXT, the SYSIN data) is not one of them.
** ----------------------------------------------------------------- */
static int
dd_is_spool_file(const JESDD *dd)
{
    return !(dd->flag & FLAG_SYSIN);
}

/* --------------------------------------------------------------------
** The one question FTPD asks RACF about the spool: may this session's ACEE
** have READ (or UPDATE) on a JESSPOOL entity?  The answer is racf_auth()'s
** return code, unmodified; ftpd_jwn_level() reads it.  This is the only
** call to racf_auth() in this file (tools/check-jes-access.py).
** ----------------------------------------------------------------- */
static int
jes_racf_auth(void *arg, const char *entity, int want_update)
{
    return racf_auth((ACEE *)arg, "JESSPOOL", entity,
                     want_update ? RACF_ATTR_UPDATE : RACF_ATTR_READ);
}

static const char *
jes_op_name(int op)
{
    switch (op) {
    case FTPD_JWN_OP_LIST:      return "LIST";
    case FTPD_JWN_OP_LISTJOB:   return "LISTJOB";
    case FTPD_JWN_OP_RETR:      return "RETR";
    default:                    return "DELE";
    }
}

/* --------------------------------------------------------------------
** May the session see, read or delete this job?
**
** The only place FTPD asks the spool policy.  The decision itself is
** ftpd_jwn_check(); this fills its context from the session, counts the
** outcome into the command's tally and writes the log line the tally asks
** for.  Returns FTPD_JWN_ALLOW, FTPD_JWN_HIDE or FTPD_JWN_NOCANCEL.
** ----------------------------------------------------------------- */
static int
job_access(ftpd_session_t *sess, const JESJOB *job, int op,
           ftpd_jwn_tally_t *tally, ftpd_jwn_cache_t *cache)
{
    ftpd_jwn_ctx_t ctx;
    ftpd_jwn_res_t res;
    char who[FTPD_JWN_PRTSZ];
    char jid[FTPD_JWN_PRTSZ];
    char rcs[16];
    const char *why;
    int decision;
    int line;

    ctx.acee_present = sess->acee != NULL;
    ctx.user = sess->user;
    ctx.auth_fn = jes_racf_auth;
    ctx.auth_arg = sess->acee;
    ctx.cache = cache;

    decision = ftpd_jwn_check(&ctx, op, (const char *)job->owner,
                              (const char *)job->jobname,
                              (const char *)job->jobid, &res);

    line = ftpd_jwn_tally(tally, &sess->jwn, op, &res);
    if (line == FTPD_JWN_LINE_NONE)
        return decision;

    ftpd_jwn_printable(who, sizeof(who), sess->user);
    ftpd_jwn_printable(jid, sizeof(jid), (const char *)job->jobid);
    if (res.rc < 0)
        strcpy(rcs, "-");
    else
        snprintf(rcs, sizeof(rcs), "%d", res.rc);

    switch (line) {
    case FTPD_JWN_LINE_ERROR:
        ftpd_log(LOG_ERROR,
                 "JES %s error: user=%s job=%s reason=ERR rc=%s elen=%d",
                 jes_op_name(op), who, jid, rcs, res.elen);
        break;
    case FTPD_JWN_LINE_NOACEE:
        ftpd_log(LOG_WARN,
                 "JES %s denied: user=%s job=%s reason=NOACEE rc=-",
                 jes_op_name(op), who, jid);
        break;
    default:
        if (decision == FTPD_JWN_NOCANCEL)
            why = "NOCANCEL";
        else if (res.reason == FTPD_JWN_R_NONE)
            why = "NONE";
        else
            why = "NOPROFILE";
        ftpd_log(LOG_WARN,
                 "JES %s denied: user=%s job=%s reason=%s rc=%s",
                 jes_op_name(op), who, jid, why, rcs);
        break;
    }
    return decision;
}

/* --------------------------------------------------------------------
** The one answer for a job that is hidden, does not pass the JESSTATUS
** filter or does not exist.  LIST JOBnnnnn, RETR and DELE all end here, so
** the three cases cannot be told apart by their reply.
** ----------------------------------------------------------------- */
static void
reply_job_not_found(ftpd_session_t *sess, const char *jobid)
{
    ftpd_session_reply(sess, FTP_550,
        "Jobid %s not found for JESJOBNAME=%s, JESSTATUS=%s, JESOWNER=%s",
        jobid,
        sess->jes_jobname[0] ? sess->jes_jobname : "*",
        status_filter(sess),
        sess->jes_owner[0]   ? sess->jes_owner   : sess->user);
}

/* --------------------------------------------------------------------
** SITE JESOWNER=<value>: may the session narrow its listing to this owner?
**
** canon receives the value to store (upper case, "" to reset).  The own
** userid and the empty value need nothing; "*" needs READ on the probe
** "..", another userid X on "X.." (ftpd#jwn.h).  A refusal is one fixed
** answer for the caller and one log line here, counted in the session's
** refusals like a refused RETR.
** ----------------------------------------------------------------- */
int
ftpd_jes_owner_ok(ftpd_session_t *sess, const char *value,
                  char *canon, int canonsz)
{
    ftpd_jwn_ctx_t ctx;
    ftpd_jwn_res_t res;
    ftpd_jwn_tally_t tally;
    char probe[FTPD_JWN_ENTSZ];
    char who[FTPD_JWN_PRTSZ];
    char what[FTPD_JWN_PRTSZ];
    char up[FTPD_JWN_PRTSZ];
    char rcs[16];
    int kind;
    int i;

    kind = ftpd_jwn_narrows(sess->user, value, canon, canonsz);
    if (kind == FTPD_JWN_NR_RESET || kind == FTPD_JWN_NR_OWN)
        return 1;

    memset(&res, 0, sizeof(res));
    res.rc = -1;
    ctx.acee_present = sess->acee != NULL;
    ctx.user = sess->user;
    ctx.auth_fn = jes_racf_auth;
    ctx.auth_arg = sess->acee;
    ctx.cache = NULL;

    if ((kind == FTPD_JWN_NR_ALL || kind == FTPD_JWN_NR_OTHER) &&
        ctx.acee_present && sess->user[0] &&
        ftpd_jwn_probe(probe, sizeof(probe),
                       kind == FTPD_JWN_NR_ALL ? "" : canon) >= 0) {
        res.level = ftpd_jwn_level(&ctx, probe, 0, &res);
        if (res.level >= FTPD_JWN_LV_READ)
            return 1;
    }

    if (canon && canonsz > 0)
        canon[0] = '\0';

    for (i = 0; i < FTPD_JWN_COMPMAX && value[i]; i++)
        up[i] = (char)toupper((unsigned char)value[i]);
    up[i] = '\0';
    ftpd_jwn_printable(what, sizeof(what), up);
    ftpd_jwn_printable(who, sizeof(who), sess->user);
    if (res.rc < 0)
        strcpy(rcs, "-");
    else
        snprintf(rcs, sizeof(rcs), "%d", res.rc);

    /* Counted like any other refusal: the same 16 lines per session. */
    ftpd_jwn_tally_init(&tally);
    res.decision = FTPD_JWN_HIDE;
    res.reason = FTPD_JWN_R_NONE;
    if (ftpd_jwn_tally(&tally, &sess->jwn, FTPD_JWN_OP_RETR, &res) ==
        FTPD_JWN_LINE_WARN)
        ftpd_log(LOG_WARN,
                 "SITE JESOWNER refused: user=%s value=%s rc=%s",
                 who, what, rcs);
    return 0;
}

/* --------------------------------------------------------------------
** The job a request names.  jesjob() answers a query such as JOB* with
** every job it matches, in no order that means anything to the caller: the
** request names a job only if its ID equals one in the list, exactly.
** ----------------------------------------------------------------- */
static JESJOB *
job_by_id(JESJOB **joblist, const char *jobid)
{
    int i;

    if (!joblist || !jobid || !jobid[0])
        return NULL;
    for (i = 0; joblist[i]; i++) {
        if (strcmp((const char *)joblist[i]->jobid, jobid) == 0)
            return joblist[i];
    }
    return NULL;
}

/* --------------------------------------------------------------------
** List spool files for a specific job (LIST JOBnnnnn).
**
** z/OS format: the job line, a line of dashes, the table of the spool files
** (ftpd#jsl.h) and their count.  Clients find the table by its ID column
** (#370).  The ID of a file is the number RETR takes after the point, so only
** the files RETR can send are listed (dd_is_spool_file()).  A job without
** such a file (still in the input queue, or its output purged) gets the job
** line alone.
**   JOBNAME  JOBID    OWNER    STATUS CLASS
**   TESTJOB  JOB00042 IBMUSER  OUTPUT A        RC=0000
**   --------
**           ID  STEPNAME PROCSTEP C DDNAME   BYTE-COUNT
**           001 JES2              H JESMSGLG       1064
**   1 spool files
** ----------------------------------------------------------------- */
static int
list_job_files(ftpd_session_t *sess, const char *jobid_arg)
{
    JES *jes = NULL;
    JESJOB **joblist = NULL;
    JESJOB *job;
    ftpd_jwn_tally_t tally;
    char jobid[16];
    char line[128];
    int visible;
    int aborted = 0;
    int nfiles;
    int id;
    int i;

    for (i = 0; jobid_arg[i] && i < (int)sizeof(jobid) - 1; i++)
        jobid[i] = (char)toupper((unsigned char)jobid_arg[i]);
    jobid[i] = '\0';

    jes = jesopen();
    if (!jes) {
        ftpd_session_reply(sess, FTP_451,
                           "Unable to open JES checkpoint");
        return 0;
    }

    /* Query with dd=1 to include JESDD array */
    joblist = jesjob(jes, jobid, FILTER_JOBID, 1);

    job = job_by_id(joblist, jobid);
    visible = 0;
    if (job && job_matches_status(sess, job)) {
        ftpd_jwn_tally_init(&tally);
        visible = job_access(sess, job, FTPD_JWN_OP_LISTJOB, &tally, NULL)
                  == FTPD_JWN_ALLOW;
    }
    if (!visible) {
        if (joblist) jesjobfr(&joblist);
        jesclose(&jes);
        reply_job_not_found(sess, jobid);
        return 0;
    }

    ftpd_session_reply(sess, FTP_125, "List started OK for %s",
                       jobid_arg);

    if (ftpd_data_open(sess) != 0) {
        jesjobfr(&joblist);
        jesclose(&jes);
        ftpd_session_reply(sess, FTP_425,
                           "Cannot open data connection");
        return 0;
    }

    /* Job header and job line, the same as in the list of the jobs */
    job_line(job, line, sizeof(line));
    if (ftpd_data_printf(sess,
        "JOBNAME  JOBID    OWNER    STATUS CLASS\r\n%s\r\n", line) < 0)
        aborted = 1;

    /* The files RETR can send, numbered the way RETR numbers them */
    nfiles = 0;
    if (job->jesdd) {
        for (i = 0; job->jesdd[i]; i++) {
            if (dd_is_spool_file(job->jesdd[i]) && nfiles < FTPD_JSL_MAXID)
                nfiles++;
        }
    }

    if (!aborted && nfiles > 0) {
        ftpd_jsl_rule(line, sizeof(line));
        if (ftpd_data_printf(sess, "%s\r\n", line) < 0)
            aborted = 1;
        ftpd_jsl_head(line, sizeof(line));
        if (!aborted && ftpd_data_printf(sess, "%s\r\n", line) < 0)
            aborted = 1;
    }

    id = 0;
    if (!aborted && nfiles > 0) {
        for (i = 0; job->jesdd[i]; i++) {
            JESDD *dd = job->jesdd[i];
            long bytecount = (long)dd->records * (dd->lrecl > 0 ? dd->lrecl : 80);
            char step[FTPD_JWN_PRTSZ];
            char proc[FTPD_JWN_PRTSZ];
            char ddn[FTPD_JWN_PRTSZ];
            char cls[2];
            char clsout[FTPD_JWN_PRTSZ];

            if (!dd_is_spool_file(dd))
                continue;
            if (id >= nfiles)
                break;
            id++;

            ftpd_jwn_printable(step, sizeof(step), (const char *)dd->stepname);
            ftpd_jwn_printable(proc, sizeof(proc), (const char *)dd->procstep);
            ftpd_jwn_printable(ddn, sizeof(ddn), (const char *)dd->ddname);
            cls[0] = (char)dd->oclass;
            cls[1] = '\0';
            ftpd_jwn_printable(clsout, sizeof(clsout),
                               dd->oclass && dd->oclass != ' ' ? cls : "");

            if (ftpd_jsl_row(line, sizeof(line), id, step, proc,
                             clsout[0] ? clsout[0] : ' ', ddn, bytecount,
                             (dd->flag & FLAG_JES2) != 0) < 0 ||
                ftpd_data_printf(sess, "%s\r\n", line) < 0) {
                aborted = 1;
                break;
            }
        }
        if (!aborted) {
            ftpd_jsl_tail(line, sizeof(line), nfiles);
            if (ftpd_data_printf(sess, "%s\r\n", line) < 0)
                aborted = 1;
        }
    }

    ftpd_data_close(sess);
    jesjobfr(&joblist);
    jesclose(&jes);

    if (aborted) {
        ftpd_session_reply(sess, FTP_426,
                           "Data connection closed, transfer aborted.");
        return 0;
    }

    ftpd_session_reply(sess, FTP_250, "List completed successfully.");
    return 0;
}

/* --------------------------------------------------------------------
** List jobs in JES queues (LIST in JES mode).
**
** If arg starts with "JOB" → spool file listing for that job.
** Otherwise → job listing with JESOWNER/JESJOBNAME/JESSTATUS filters.
**
** A job is listed if it passes the filters and the spool policy allows it
** (job_access).  A hidden job is not printed and not announced; only the
** log counts it.
**
** z/OS format:
**   JOBNAME  JOBID    OWNER    STATUS CLASS
**   TESTJOB  JOB00042 IBMUSER  OUTPUT A     RC=0000
** ----------------------------------------------------------------- */
int
ftpd_jes_list(ftpd_session_t *sess, const char *arg)
{
    JES *jes = NULL;
    JESJOB **joblist = NULL;
    JESFILT filt_type;
    ftpd_jwn_tally_t tally;
    ftpd_jwn_cache_t cache;
    const char *filter;
    const char *owner;
    int i;
    int count;
    int aborted = 0;

    /* If arg looks like a job ID (JOBnnnnn), list spool files */
    if (arg && arg[0] &&
        (arg[0] == 'J' || arg[0] == 'j') &&
        (arg[1] == 'O' || arg[1] == 'o') &&
        (arg[2] == 'B' || arg[2] == 'b')) {
        return list_job_files(sess, arg);
    }

    /* Determine filter from SITE settings */
    if (sess->jes_jobname[0]) {
        filter = sess->jes_jobname;
        filt_type = FILTER_JOBNAME;
    } else {
        filter = "";
        filt_type = FILTER_NONE;
    }

    owner = sess->jes_owner[0] ? sess->jes_owner : sess->user;

    jes = jesopen();
    if (!jes) {
        ftpd_session_reply(sess, FTP_451,
                           "Unable to open JES checkpoint");
        return 0;
    }

    /* Query jobs — dd=0 (no spool file details needed for job list) */
    joblist = jesjob(jes, filter, filt_type, 0);

    /* z/OS: "125 List started OK for JESJOBNAME=..., JESSTATUS=ALL and JESOWNER=..." */
    ftpd_session_reply(sess, FTP_125,
        "List started OK for JESJOBNAME=%s, JESSTATUS=%s and JESOWNER=%s",
        sess->jes_jobname[0] ? sess->jes_jobname : "*",
        status_filter(sess),
        owner[0] == '*' || !owner[0] ? "*" : owner);

    if (ftpd_data_open(sess) != 0) {
        if (joblist) jesjobfr(&joblist);
        jesclose(&jes);
        ftpd_session_reply(sess, FTP_425,
                           "Cannot open data connection");
        return 0;
    }

    ftpd_jwn_tally_init(&tally);
    ftpd_jwn_cache_init(&cache);

    /* Header — z/OS format */
    if (ftpd_data_printf(sess,
        "JOBNAME  JOBID    OWNER    STATUS CLASS\r\n") < 0)
        aborted = 1;

    /* Iterate jobs — respect JESENTRYLIMIT (default 200).  The cheap
    ** filters come first, the spool policy last: it may ask RACF. */
    count = 0;
    if (joblist && !aborted) {
        for (i = 0; joblist[i]; i++) {
            JESJOB *job = joblist[i];
            char line[128];

            if (!ftpd_jwn_match(owner, (const char *)job->owner))
                continue;

            if (!job_matches_status(sess, job))
                continue;

            if (job_access(sess, job, FTPD_JWN_OP_LIST, &tally, &cache) !=
                FTPD_JWN_ALLOW)
                continue;

            if (count >= 200) {
                /* z/OS: JESENTRYLIMIT reached */
                break;
            }

            job_line(job, line, sizeof(line));
            if (ftpd_data_printf(sess, "%s\r\n", line) < 0) {
                aborted = 1;
                break;
            }
            count++;
        }
    }

    /* One exit for every way out of the loop: the lines the log owes the
    ** operator are written before anything is released. */
    if (ftpd_jwn_tally_more(&tally) > 0) {
        char who[FTPD_JWN_PRTSZ];

        ftpd_jwn_printable(who, sizeof(who), sess->user);
        ftpd_log(LOG_ERROR, "JES LIST error: user=%s and %d more ERR jobs",
                 who, ftpd_jwn_tally_more(&tally));
    }
    ftpd_log(LOG_INFO,
             "JES LIST: %d jobs, %d own, %d hidden, %d ERR, %d RACF calls",
             count, tally.own, tally.hidden, tally.err, tally.racf_calls);

    ftpd_data_close(sess);

    if (joblist) jesjobfr(&joblist);
    jesclose(&jes);

    if (aborted) {
        ftpd_session_reply(sess, FTP_426,
                           "Data connection closed, transfer aborted.");
        return 0;
    }

    if (count >= 200) {
        ftpd_session_reply_multi(sess, FTP_250,
            "JESENTRYLIMIT of 200 reached.  Additional entries not displayed",
            "List completed successfully.");
    } else {
        ftpd_session_reply(sess, FTP_250, "List completed successfully.");
    }
    return 0;
}

/* ====================================================================
** JES Spool Retrieval — RETR in JES mode
** ==================================================================== */

/* --------------------------------------------------------------------
** jesprint() callback: send one spool line to the FTP data connection.
** The session arrives in jesprint()'s arg parameter (libc370 #21/#22);
** the former detour through grtapp1 is no longer needed.
** Lines are in EBCDIC — translate to ASCII for TYPE A.
**
** A failed send returns -1, which stops the walk (JESPR_STOPPED) instead
** of reading the rest of the spool data set into a dead socket.
** ----------------------------------------------------------------- */
static int
spool_line_callback(const char *line, unsigned linelen, void *arg)
{
    ftpd_session_t *sess = (ftpd_session_t *)arg;
    char buf[256];
    int len;

    if (!sess || !line) return -1;

    len = (int)linelen;
    if (len > (int)sizeof(buf) - 3) len = (int)sizeof(buf) - 3;

    memcpy(buf, line, len);

    /* Translate EBCDIC→ASCII if TYPE A */
    if (sess->type == XFER_TYPE_A)
        ftpd_xlat_mvs_e2a((unsigned char *)buf, len);

    /* ASCII CRLF — must use explicit values, not C literals
    ** which are EBCDIC on c2asm370 ('\n' = 0x15 NEL, not 0x0A LF) */
    buf[len]     = 0x0D;   /* ASCII CR */
    buf[len + 1] = 0x0A;   /* ASCII LF */

    if (ftpd_data_send(sess, buf, len + 2) < 0)
        return -1;         /* jesprint() stops with JESPR_STOPPED */

    return 0;
}

/* --------------------------------------------------------------------
** Helper: find a job by job ID, return job + joblist for cleanup.
** Follows mvsMF find_job_by_name_and_id() pattern.
**
** The job is returned only if it passes the JESSTATUS filter and the spool
** policy for op.  *decision tells ALLOW from NOCANCEL (a delete the
** session may not do); a hidden job is NULL, the same as a missing one.
** ----------------------------------------------------------------- */
static JESJOB *
find_job(ftpd_session_t *sess, const char *jobid_arg, int op,
         JESJOB ***out_joblist, int *decision)
{
    JES *jes;
    JESJOB **joblist;
    JESJOB *job;
    ftpd_jwn_tally_t tally;

    *out_joblist = NULL;
    *decision = FTPD_JWN_HIDE;

    jes = jesopen();
    if (!jes) return NULL;

    joblist = jesjob(jes, jobid_arg, FILTER_JOBID, 1);
    jesclose(&jes);

    job = job_by_id(joblist, jobid_arg);
    if (!job || !job_matches_status(sess, job)) {
        if (joblist) jesjobfr(&joblist);
        return NULL;
    }

    ftpd_jwn_tally_init(&tally);
    *decision = job_access(sess, job, op, &tally, NULL);
    if (*decision == FTPD_JWN_HIDE) {
        jesjobfr(&joblist);
        return NULL;
    }

    *out_joblist = joblist;
    return job;
}

/* --------------------------------------------------------------------
** Retrieve spool output for a job.
**
** RETR JOBnnnnn   → all spool files concatenated (separator between)
** RETR JOBnnnnn.n → specific spool file (1-based index)
**
** Uses jesprint() with callback to send lines on data connection.
** jesprint() reports why its block walk stopped (JESPRST), which is what
** tells an empty spool file apart from one JES2 has already purged.
** ----------------------------------------------------------------- */
int
ftpd_jes_retrieve(ftpd_session_t *sess, const char *arg)
{
    char jobid_arg[16];
    int dsid_req;       /* -1 = all, >=0 = specific spool file index */
    JESJOB **joblist = NULL;
    JESJOB *job;
    JES *jes = NULL;
    JESPRST st;
    unsigned lines = 0; /* lines handed to the data connection           */
    int stale = 0;      /* spool files the checkpoint lists but JES2 purged */
    int io_err = 0;     /* spool read failed / walk truncated            */
    int aborted = 0;    /* our callback stopped: data connection is gone */
    int i;
    int dd_index;
    int decision;
    int rc;

    if (!arg || !arg[0]) {
        ftpd_session_reply(sess, FTP_501, "Missing job ID");
        return 0;
    }

    /* Parse "JOBnnnnn" or "JOBnnnnn.n" */
    {
        const char *dot = strchr(arg, '.');
        if (dot) {
            int idlen = (int)(dot - arg);
            if (idlen > 15) idlen = 15;
            memcpy(jobid_arg, arg, idlen);
            jobid_arg[idlen] = '\0';
            dsid_req = atoi(dot + 1);   /* 1-based index */
        } else {
            strncpy(jobid_arg, arg, sizeof(jobid_arg) - 1);
            jobid_arg[sizeof(jobid_arg) - 1] = '\0';
            dsid_req = -1;              /* all spool files */
        }
    }

    /* Uppercase */
    {
        int k;
        for (k = 0; jobid_arg[k]; k++)
            jobid_arg[k] = (char)toupper((unsigned char)jobid_arg[k]);
    }

    /* The shape first: a wildcard must not reach jesjob() (#267) */
    if (!ftpd_jid_valid(jobid_arg)) {
        ftpd_session_reply(sess, FTP_501, "Invalid job ID");
        return 0;
    }

    /* Find the job */
    job = find_job(sess, jobid_arg, FTPD_JWN_OP_RETR, &joblist, &decision);
    if (!job) {
        /* z/OS: "550 Jobid J0099999 not found for JESJOBNAME=..., JESSTATUS=..., JESOWNER=..." */
        reply_job_not_found(sess, jobid_arg);
        return 0;
    }

    /* z/OS 125 response differs for all vs specific spool file */
    if (dsid_req < 0) {
        ftpd_session_reply(sess, FTP_125,
            "Sending all spool files for requested Jobid");
    } else {
        /* z/OS: "125 Sending data set owner.jobname.jobid.Dseqno.ddname" */
        JESDD *target_dd = NULL;
        int idx = 0;
        if (job->jesdd) {
            int j;
            for (j = 0; job->jesdd[j]; j++) {
                if (!dd_is_spool_file(job->jesdd[j])) continue;
                idx++;
                if (idx == dsid_req) {
                    target_dd = job->jesdd[j];
                    break;
                }
            }
        }
        /* An index past the last spool file (or 0 -- DD numbering starts at
        ** 1) has nothing to send.  Say so before the 125: announcing a data
        ** set and then completing an empty transfer claims the spool file
        ** exists and is empty, which is a different answer. */
        if (!target_dd) {
            jesjobfr(&joblist);
            ftpd_session_reply(sess, FTP_550,
                "Spool file %d not found for Jobid %s, %d spool file(s) available",
                dsid_req, jobid_arg, idx);
            return 0;
        }

        char own[FTPD_JWN_PRTSZ];
        char jnm[FTPD_JWN_PRTSZ];
        char jid[FTPD_JWN_PRTSZ];
        char ddn[FTPD_JWN_PRTSZ];

        ftpd_jwn_printable(own, sizeof(own), (const char *)job->owner);
        ftpd_jwn_printable(jnm, sizeof(jnm), (const char *)job->jobname);
        ftpd_jwn_printable(jid, sizeof(jid), (const char *)job->jobid);
        ftpd_jwn_printable(ddn, sizeof(ddn), (const char *)target_dd->ddname);
        ftpd_session_reply(sess, FTP_125,
            "Sending data set %s.%s.%s.D%07d.%s",
            own, jnm, jid, target_dd->dsid, ddn);
    }

    if (ftpd_data_open(sess) != 0) {
        jesjobfr(&joblist);
        ftpd_session_reply(sess, FTP_425,
                           "Cannot open data connection");
        return 0;
    }

    /* Open JES for spool reading */
    jes = jesopen();
    if (!jes) {
        ftpd_data_close(sess);
        jesjobfr(&joblist);
        ftpd_session_reply(sess, FTP_451,
                           "Unable to open JES checkpoint");
        return 0;
    }

    /* Iterate spool files */
    dd_index = 0;
    if (job->jesdd) {
        for (i = 0; job->jesdd[i]; i++) {
            JESDD *dd = job->jesdd[i];

            /* Skip SYSIN entries (don't count in index) */
            if (!dd_is_spool_file(dd))
                continue;

            dd_index++;     /* 1-based index of visible spool files */

            /* If specific spool file requested, skip others */
            if (dsid_req >= 0 && dd_index != dsid_req)
                continue;

            /* Skip empty spool files */
            if (!dd->mttr)
                continue;

            rc = jesprint(jes, job, dd->dsid, spool_line_callback,
                          sess, &st);

            ftpd_trace("RETR dsid=%d rc=%d reason=%d blocks=%u lines=%u",
                       dd->dsid, rc, st.reason, st.blocks, st.lines);

            lines += st.lines;

            if (rc > 0) {
                /* 503 = JES unusable, 404 = dsid not in this job */
                io_err = 1;
                ftpd_log(LOG_ERROR,
                         "JES RETR: jesprint rc=%d dsid=%d", rc, dd->dsid);
            } else if (st.reason == JESPR_FOREIGN ||
                       st.reason == JESPR_DSID) {
                /* The very first block belongs to somebody else: nothing of
                ** this data set was read.  JES2 printed and purged it while
                ** the checkpoint still advertises it, and its tracks have
                ** been reallocated.  An empty transfer would claim the data
                ** set is empty, which is not the same thing. */
                stale++;
                ftpd_log(LOG_WARN,
                         "JES RETR: dsid=%d no longer on spool (reason=%d)",
                         dd->dsid, st.reason);
            } else if (st.reason == JESPR_STOPPED) {
                aborted = 1;
                ftpd_log(LOG_ERROR,
                         "JES RETR: data connection lost, dsid=%d prtrc=%d",
                         dd->dsid, st.prtrc);
            } else if (st.reason != JESPR_END &&
                       st.reason != JESPR_EMPTY &&
                       st.reason != JESPR_OPENEND) {
                /* IOERR, LOOP, CAP, NOBUF, NOMEM: what was sent is
                ** incomplete.  JESPR_OPENEND is NOT one of these -- it is
                ** the normal end of a data set of a still running job,
                ** whose last block chains to a track that is allocated but
                ** not yet written and therefore carries a foreign key. */
                io_err = 1;
                ftpd_log(LOG_ERROR,
                         "JES RETR: spool read failed, dsid=%d reason=%d "
                         "mttr=%u", dd->dsid, st.reason, st.mttr);
            }

            if (aborted)
                break;

            /* z/OS separator: space-prefixed, exact string */
            if (dsid_req < 0) {
                ftpd_data_printf(sess,
                    " !! END OF JES SPOOL FILE !!\r\n");
            }
        }
    }

    jesclose(&jes);
    ftpd_data_close(sess);
    jesjobfr(&joblist);

    if (aborted) {
        ftpd_session_reply(sess, FTP_426,
                           "Data connection closed, transfer aborted.");
        return 0;
    }

    if (io_err) {
        ftpd_session_reply(sess, FTP_451,
                           "Error reading spool data set for %s.",
                           jobid_arg);
        return 0;
    }

    if (lines == 0 && stale > 0) {
        /* Nothing was read and the checkpoint entry is stale */
        ftpd_session_reply(sess, FTP_550,
                           "Spool data set for %s no longer available.",
                           jobid_arg);
        return 0;
    }

    if (stale > 0) {
        ftpd_log(LOG_WARN,
                 "JES RETR: %s, %d spool file(s) no longer on spool",
                 jobid_arg, stale);
    }

    ftpd_session_reply(sess, FTP_250,
                       "Transfer completed successfully.");
    return 0;
}

/* ====================================================================
** JES Job Purge — DELE in JES mode
** ==================================================================== */

/* --------------------------------------------------------------------
** Delete (purge) a job from JES queues.
**
** DELE JOBnnnnn → jescanj(jobname, jobid, 1)
** Follows mvsMF jobPurgeHandler() error mapping.
** ----------------------------------------------------------------- */
int
ftpd_jes_delete(ftpd_session_t *sess, const char *arg)
{
    char jobid_arg[16];
    JESJOB **joblist = NULL;
    JESJOB *job;
    char jnm[FTPD_JWN_PRTSZ];
    char jid[FTPD_JWN_PRTSZ];
    int decision;
    int rc;

    if (!arg || !arg[0]) {
        ftpd_session_reply(sess, FTP_501, "Missing job ID");
        return 0;
    }

    /* Uppercase */
    strncpy(jobid_arg, arg, sizeof(jobid_arg) - 1);
    jobid_arg[sizeof(jobid_arg) - 1] = '\0';
    {
        int k;
        for (k = 0; jobid_arg[k]; k++)
            jobid_arg[k] = (char)toupper((unsigned char)jobid_arg[k]);
    }

    /* The shape first: a wildcard must not reach jesjob() (#267) */
    if (!ftpd_jid_valid(jobid_arg)) {
        ftpd_session_reply(sess, FTP_501, "Invalid job ID");
        return 0;
    }

    /* Find the job to get the jobname (jescanj needs both).  Nothing is
    ** cancelled before find_job() has seen the spool policy allow it. */
    job = find_job(sess, jobid_arg, FTPD_JWN_OP_DELE, &joblist, &decision);
    if (!job) {
        /* z/OS: "550 Jobid J0099999 not found for JESJOBNAME=..., ..." */
        reply_job_not_found(sess, jobid_arg);
        return 0;
    }

    if (decision == FTPD_JWN_NOCANCEL) {
        jesjobfr(&joblist);
        ftpd_session_reply(sess, FTP_550,
                           "Access denied - cannot cancel %s", jobid_arg);
        return 0;
    }

    ftpd_jwn_printable(jnm, sizeof(jnm), (const char *)job->jobname);
    ftpd_jwn_printable(jid, sizeof(jid), (const char *)job->jobid);
    ftpd_log(LOG_INFO, "JES DELE: purging %s (%s)", jnm, jid);

    rc = jescanj((const char *)job->jobname,
                 (const char *)job->jobid, 1);

    jesjobfr(&joblist);

    switch (rc) {
    case CANJ_OK:
        /* z/OS: "250 Cancel successful" */
        ftpd_session_reply(sess, FTP_250, "Cancel successful");
        break;
    case CANJ_NOJB:
    case CANJ_BADI:
        reply_job_not_found(sess, jobid_arg);
        break;
    case CANJ_ICAN:
        ftpd_session_reply(sess, FTP_550,
                           "Access denied - cannot cancel %s", jobid_arg);
        break;
    default:
        ftpd_log(LOG_ERROR, "JES DELE: jescanj rc=%d for %s",
                 rc, jobid_arg);
        ftpd_session_reply(sess, FTP_550,
                           "Failed to cancel %s (rc=%d)",
                           jobid_arg, rc);
        break;
    }

    return 0;
}
