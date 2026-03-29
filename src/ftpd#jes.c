/*
** FTPD JES Interface — Job Submission
**
** Submits JCL received on the FTP data connection to JES2 via the
** internal reader API (jesiropn/jesirput/jesircls).
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
#include "clibwto.h"

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
    memset(card, ' ', 80);
    if (linelen > 0)
        memcpy(card, line, linelen);
    card[80] = '\0';

    /* Translate the full 80-byte card ASCII→EBCDIC (CP037) */
    if (sess->type == XFER_TYPE_A)
        ftpd_xlat_mvs_a2e((unsigned char *)card, 80);
}

/* --------------------------------------------------------------------
** Helper: submit one card to the internal reader with wtof debug.
** Returns jesirput() return code.
** ----------------------------------------------------------------- */
static int
submit_card(VSFILE *intrdr, char *card, int cardnum)
{
    wtof("FTPD JES CARD %03d: %.80s", cardnum, card);
    return jesirput(intrdr, card);
}

/* --------------------------------------------------------------------
** Helper: inject USER= and PASSWORD= continuation cards after JOB card.
** Follows mvsmf jobsapi.c process_jobcard() lines 1590-1625.
** ----------------------------------------------------------------- */
static int
inject_user_pass(ftpd_session_t *sess, VSFILE *intrdr, int *cardcount)
{
    char card[81];
    int rc;

    /* USER= continuation card (in ASCII, then convert) */
    {
        char asc[81];
        memset(asc, ' ', 80);
        snprintf(asc, 80, "//         USER=%s,", sess->user);
        asc[strlen(asc)] = ' ';    /* overwrite snprintf null with blank */
        asc[80] = '\0';
        ftpd_xlat_mvs_a2e((unsigned char *)asc, 80);
        memcpy(card, asc, 81);
    }

    rc = submit_card(intrdr, card, *cardcount + 1);
    if (rc < 0) return rc;
    (*cardcount)++;

    /* PASSWORD= continuation card */
    {
        char asc[81];
        memset(asc, ' ', 80);
        snprintf(asc, 80, "//         PASSWORD=%s", sess->pass);
        asc[strlen(asc)] = ' ';
        asc[80] = '\0';
        ftpd_xlat_mvs_a2e((unsigned char *)asc, 80);
        memcpy(card, asc, 81);
    }

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
    ** 2 = JOB card complete, USER/PASS injected */
    int job_state;

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

                    /* Inject USER= and PASSWORD= */
                    rc = inject_user_pass(sess, intrdr, &cardcount);
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
