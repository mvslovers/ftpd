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
** 4. Detect JOB card, inject USER= and PASSWORD= continuation lines
** 5. jesirput() per card, jesircls() to trigger JES2 processing
**
** USER/PASSWORD injection follows mvsmf/src/jobsapi.c process_jobcard().
*/
#include "ftpd.h"
#include "ftpd#ses.h"
#include "ftpd#dat.h"
#include "ftpd#jes.h"
#include "ftpd#xlt.h"
#include "clibjes2.h"
#include "clibgrt.h"
#include "haspjqe.h"

/* ASCII constants (wire format before translation) */
#define ASCII_LF    0x0A
#define ASCII_CR    0x0D

/* EBCDIC constants (after CP037 translation) */
#define EBC_BLANK   0x40
#define EBC_COMMA   0x6B

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
    int linepos;
    int rc;
    int cardcount;
    long total;
    char c;

    /* JOB card detection state:
    ** 0 = haven't seen JOB yet
    ** 1 = inside JOB card (may have continuations)
    ** 2 = JOB card complete, params injected */
    int job_state;
    int has_notify;     /* 1 if NOTIFY= found in any JOB card line */

    /* Reply 125 before opening data connection */
    ftpd_session_reply(sess, FTP_125, "Submitting JCL to internal reader");

    if (ftpd_data_open(sess) != 0) {
        ftpd_session_reply(sess, FTP_425,
                           "Cannot open data connection");
        return 0;
    }

    /* Open JES2 internal reader */
    rc = jesiropn(&intrdr);
    if (rc < 0) {
        ftpd_log(LOG_ERROR, "JES: jesiropn() failed rc=%d", rc);
        ftpd_data_close(sess);
        ftpd_session_reply(sess, FTP_451,
                           "Unable to open JES internal reader");
        return 0;
    }

    ftpd_log(LOG_INFO, "JES: internal reader opened, receiving JCL");

    linepos = 0;
    cardcount = 0;
    total = 0;
    job_state = 0;
    has_notify = 0;

    /* Read byte-by-byte, split at ASCII LF (0x0A) */
    while (ftpd_data_recv(sess, &c, 1) == 1) {
        total++;

        if (c == ASCII_LF) {
            /* End of line — strip trailing CR if present */
            if (linepos > 0 && line[linepos - 1] == ASCII_CR)
                linepos--;

            /* Build 80-byte EBCDIC card from this line */
            build_card(sess, line, linepos, card);

            /* JOB card detection + USER/PASSWORD injection */
            if (job_state == 0) {
                /* Look for " JOB " in the EBCDIC card.
                ** After a2e: ' '=0x40, 'J'=0xD1, 'O'=0xD6, 'B'=0xC2 */
                unsigned char *p;
                for (p = (unsigned char *)card; p < (unsigned char *)card + 76; p++) {
                    if (p[0] == 0x40 && p[1] == 0xD1 &&
                        p[2] == 0xD6 && p[3] == 0xC2 && p[4] == 0x40) {
                        job_state = 1;
                        break;
                    }
                }
            }

            if (job_state == 1) {
                /* Scan this JOB card line for NOTIFY (EBCDIC).
                ** C literal "NOTIFY" is EBCDIC on c2asm370, card is
                ** also EBCDIC after build_card, so strstr works. */
                if (strstr(card, "NOTIFY") != NULL)
                    has_notify = 1;

                /* Check if JOB card ends (no trailing comma = last JOB line).
                ** Find last non-blank EBCDIC byte. */
                int len = 80;
                while (len > 0 && (unsigned char)card[len - 1] == EBC_BLANK)
                    len--;

                if (len > 0 && (unsigned char)card[len - 1] != EBC_COMMA) {
                    /* Last JOB card line — append comma for continuation */
                    if (len < 71) {
                        card[len] = (char)EBC_COMMA;
                    }

                    /* Submit the modified JOB card */
                    rc = submit_card(intrdr, card, cardcount + 1);
                    if (rc < 0) goto fail;
                    cardcount++;

                    /* Inject NOTIFY (if missing), USER, PASSWORD */
                    rc = inject_job_params(sess, intrdr, &cardcount,
                                           has_notify);
                    if (rc < 0) goto fail;

                    job_state = 2;
                    linepos = 0;
                    continue;   /* skip normal submit — already done */
                }
                /* else: JOB card continues (ends with comma), submit normally
                ** and check next line */
            }

            /* Normal card submission */
            rc = submit_card(intrdr, card, cardcount + 1);
            if (rc < 0) goto fail;
            cardcount++;

            /* If we just submitted a JOB continuation line that was NOT the
            ** last one, check if the NEXT line is still a continuation.
            ** A continuation starts with "// " (cols 1-2 = //, col 3 = blank).
            ** We check this on the NEXT iteration when job_state == 1. */

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
        rc = submit_card(intrdr, card, cardcount + 1);
        if (rc < 0) goto fail;
        cardcount++;
    }

    ftpd_data_close(sess);

    ftpd_log(LOG_INFO, "JES: closing internal reader, %d cards, %ld bytes",
             cardcount, total);

    /* Close internal reader — triggers JES2 processing */
    rc = jesircls(intrdr);
    if (rc < 0) {
        ftpd_log(LOG_ERROR, "JES: jesircls() failed rc=%d", rc);
        ftpd_session_reply(sess, FTP_451,
                           "Failed to close internal reader");
        return 0;
    }

    /* Extract job ID from JES2 response (8 bytes in rpl.rplrbar) */
    memset(jobid, 0, sizeof(jobid));
    strncpy(jobid, (const char *)intrdr->rpl.rplrbar, 8);
    jobid[8] = '\0';

    ftpd_log(LOG_INFO, "JES: job submitted: %s (%d cards, %ld bytes)",
             jobid, cardcount, total);

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

    sess->bytes_recv += total;
    sess->xfer_count++;
    if (sess->server)
        sess->server->total_bytes_in += total;

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
** Helper: check if job matches owner filter.
** Follows mvsmf jobsapi.c should_skip_job().
** ----------------------------------------------------------------- */
static int
job_matches_owner(JESJOB *job, const char *owner)
{
    if (!owner || owner[0] == '\0' || owner[0] == '*')
        return 1;   /* no filter or wildcard = match all */
    if (job->owner[0] == '\0')
        return 0;
    return (strncmp((const char *)job->owner, owner,
                    strlen(owner)) == 0);
}

/* --------------------------------------------------------------------
** List spool files for a specific job (LIST JOBnnnnn).
** z/OS format:
**   JOBNAME  JOBID    STEPNAME PROCSTEP C DDNAME   BYTE-COUNT
** ----------------------------------------------------------------- */
static int
list_job_files(ftpd_session_t *sess, const char *jobid_arg)
{
    JES *jes = NULL;
    JESJOB **joblist = NULL;
    JESJOB *job;
    int i;

    jes = jesopen();
    if (!jes) {
        ftpd_session_reply(sess, FTP_451,
                           "Unable to open JES checkpoint");
        return 0;
    }

    /* Query with dd=1 to include JESDD array */
    joblist = jesjob(jes, jobid_arg, FILTER_JOBID, 1);

    if (!joblist || !joblist[0]) {
        if (joblist) jesjobfr(&joblist);
        jesclose(&jes);
        ftpd_session_reply(sess, FTP_550,
                           "No job found with ID %s", jobid_arg);
        return 0;
    }

    job = joblist[0];

    ftpd_session_reply(sess, FTP_125, "List started OK for %s",
                       jobid_arg);

    if (ftpd_data_open(sess) != 0) {
        jesjobfr(&joblist);
        jesclose(&jes);
        ftpd_session_reply(sess, FTP_425,
                           "Cannot open data connection");
        return 0;
    }

    /* Header */
    ftpd_data_printf(sess,
        "JOBNAME  JOBID    STEPNAME PROCSTEP C DDNAME   BYTE-COUNT\r\n");

    /* Iterate spool files */
    if (job->jesdd) {
        for (i = 0; job->jesdd[i]; i++) {
            JESDD *dd = job->jesdd[i];
            long bytecount = (long)dd->records * (dd->lrecl > 0 ? dd->lrecl : 80);

            ftpd_data_printf(sess,
                "%-8.8s %-8.8s %-8.8s %-8.8s %c %-8.8s %10ld\r\n",
                job->jobname, job->jobid,
                dd->stepname, dd->procstep,
                dd->oclass ? dd->oclass : ' ',
                dd->ddname,
                bytecount);
        }
    }

    ftpd_data_close(sess);
    jesjobfr(&joblist);
    jesclose(&jes);

    ftpd_session_reply(sess, FTP_250, "List completed successfully.");
    return 0;
}

/* --------------------------------------------------------------------
** List jobs in JES queues (LIST in JES mode).
**
** If arg starts with "JOB" → spool file listing for that job.
** Otherwise → job listing with JESOWNER/JESJOBNAME/JESSTATUS filters.
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
    const char *filter;
    const char *owner;
    int i;
    int count;
    char rcbuf[16];

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

    ftpd_session_reply(sess, FTP_125, "List started OK");

    if (ftpd_data_open(sess) != 0) {
        if (joblist) jesjobfr(&joblist);
        jesclose(&jes);
        ftpd_session_reply(sess, FTP_425,
                           "Cannot open data connection");
        return 0;
    }

    /* Header */
    ftpd_data_printf(sess,
        "JOBNAME  JOBID    OWNER    STATUS CLASS\r\n");

    /* Iterate jobs */
    count = 0;
    if (joblist) {
        for (i = 0; joblist[i]; i++) {
            JESJOB *job = joblist[i];
            const char *status;
            const char *rc_str;

            if (!job_matches_owner(job, owner))
                continue;

            status = job_status(job->q_type);
            rc_str = job_retcode(job, rcbuf, sizeof(rcbuf));

            ftpd_data_printf(sess,
                "%-8.8s %-8.8s %-8.8s %-6s  %-5c %s\r\n",
                job->jobname, job->jobid, job->owner,
                status,
                job->eclass ? job->eclass : ' ',
                rc_str);
            count++;
        }
    }

    ftpd_data_close(sess);

    if (joblist) jesjobfr(&joblist);
    jesclose(&jes);

    ftpd_log(LOG_INFO, "JES LIST: %d jobs", count);
    ftpd_session_reply(sess, FTP_250, "List completed successfully.");
    return 0;
}

/* ====================================================================
** JES Spool Retrieval — RETR in JES mode
** ==================================================================== */

/* --------------------------------------------------------------------
** jesprint() callback: send one spool line to the FTP data connection.
** Session pointer is passed via grtapp1 (same pattern as mvsMF).
** Lines are in EBCDIC — translate to ASCII for TYPE A.
** ----------------------------------------------------------------- */
static int
spool_line_callback(const char *line, unsigned linelen)
{
    CLIBGRT *grt = __grtget();
    ftpd_session_t *sess = (ftpd_session_t *)grt->grtapp1;
    char buf[256];
    int len;

    if (!sess || !line) return -1;

    len = (int)linelen;
    if (len > (int)sizeof(buf) - 3) len = (int)sizeof(buf) - 3;

    memcpy(buf, line, len);

    /* Translate EBCDIC→ASCII if TYPE A */
    if (sess->type == XFER_TYPE_A)
        ftpd_xlat_mvs_e2a((unsigned char *)buf, len);

    buf[len]     = '\r';
    buf[len + 1] = '\n';

    ftpd_data_send(sess, buf, len + 2);
    return 0;
}

/* --------------------------------------------------------------------
** Helper: find a job by job ID, return job + joblist for cleanup.
** Follows mvsMF find_job_by_name_and_id() pattern.
** ----------------------------------------------------------------- */
static JESJOB *
find_job(const char *jobid_arg, JESJOB ***out_joblist)
{
    JES *jes;
    JESJOB **joblist;

    *out_joblist = NULL;

    jes = jesopen();
    if (!jes) return NULL;

    joblist = jesjob(jes, jobid_arg, FILTER_JOBID, 1);
    jesclose(&jes);

    if (!joblist || !joblist[0]) {
        if (joblist) jesjobfr(&joblist);
        return NULL;
    }

    *out_joblist = joblist;
    return joblist[0];
}

/* --------------------------------------------------------------------
** Retrieve spool output for a job.
**
** RETR JOBnnnnn   → all spool files concatenated (separator between)
** RETR JOBnnnnn.n → specific spool file (1-based index)
**
** Uses jesprint() with callback to send lines on data connection.
** ----------------------------------------------------------------- */
int
ftpd_jes_retrieve(ftpd_session_t *sess, const char *arg)
{
    char jobid_arg[16];
    int dsid_req;       /* -1 = all, >=0 = specific spool file index */
    JESJOB **joblist = NULL;
    JESJOB *job;
    JES *jes = NULL;
    CLIBGRT *grt;
    void *saved_app1;
    int i;
    int dd_index;
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

    /* Find the job */
    job = find_job(jobid_arg, &joblist);
    if (!job) {
        ftpd_session_reply(sess, FTP_550,
                           "No job found with ID %s", jobid_arg);
        return 0;
    }

    ftpd_session_reply(sess, FTP_125,
                       "Sending spool output for %s", jobid_arg);

    if (ftpd_data_open(sess) != 0) {
        jesjobfr(&joblist);
        ftpd_session_reply(sess, FTP_425,
                           "Cannot open data connection");
        return 0;
    }

    /* Store session in grtapp1 for the jesprint callback */
    grt = __grtget();
    saved_app1 = grt->grtapp1;
    grt->grtapp1 = sess;

    /* Open JES for spool reading */
    jes = jesopen();
    if (!jes) {
        grt->grtapp1 = saved_app1;
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
            dd_index++;

            /* Skip SYSIN entries */
            if (dd->flag & FLAG_SYSIN)
                continue;

            /* If specific spool file requested, skip others */
            if (dsid_req >= 0 && dd_index != dsid_req)
                continue;

            /* Skip empty spool files */
            if (!dd->mttr)
                continue;

            rc = jesprint(jes, job, dd->dsid, spool_line_callback);
            if (rc < 0) {
                ftpd_log(LOG_ERROR,
                         "JES RETR: jesprint failed rc=%d dsid=%d",
                         rc, dd->dsid);
            }

            /* Separator between spool files (only for "all" mode) */
            if (dsid_req < 0) {
                ftpd_data_printf(sess,
                    "!! END OF JES SPOOL FILE !!\r\n");
            }
        }
    }

    /* Restore grtapp1 */
    grt->grtapp1 = saved_app1;

    jesclose(&jes);
    ftpd_data_close(sess);
    jesjobfr(&joblist);

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

    /* Find the job to get the jobname (jescanj needs both) */
    job = find_job(jobid_arg, &joblist);
    if (!job) {
        ftpd_session_reply(sess, FTP_550,
                           "Job %s not found", jobid_arg);
        return 0;
    }

    ftpd_log(LOG_INFO, "JES DELE: purging %s (%s)",
             job->jobname, job->jobid);

    rc = jescanj((const char *)job->jobname,
                 (const char *)job->jobid, 1);

    jesjobfr(&joblist);

    switch (rc) {
    case CANJ_OK:
        ftpd_session_reply(sess, FTP_250,
                           "%s cancelled", jobid_arg);
        break;
    case CANJ_NOJB:
    case CANJ_BADI:
        ftpd_session_reply(sess, FTP_550,
                           "Job %s not found", jobid_arg);
        break;
    case CANJ_ICAN:
        ftpd_session_reply(sess, FTP_550,
                           "Access denied — cannot cancel %s", jobid_arg);
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
