/*
** FTPD JES Interface — Job Submission
**
** Submits JCL received on the FTP data connection to JES2 via the
** internal reader API (jesiropn/jesirput/jesircls).
**
** Pattern follows mvsmf/src/jobsapi.c submit_jcl_content() exactly:
** 1. Receive ALL JCL from data connection into a buffer
** 2. Translate entire buffer ASCII→EBCDIC (CP037)
** 3. Tokenize at EBCDIC NEL (0x15) — ASCII LF maps to NEL in CP037
** 4. Each token = one JCL card, padded to 80 with EBCDIC blanks
** 5. jesirput() per card, jesircls() to trigger JES2 processing
** 6. Extract job ID from intrdr->rpl.rplrbar
*/
#include "ftpd.h"
#include "ftpd#ses.h"
#include "ftpd#dat.h"
#include "ftpd#jes.h"
#include "ftpd#xlt.h"
#include "clibjes2.h"
#include "clibwto.h"

/* Maximum JCL size we accept (256 KB should be plenty) */
#define MAX_JCL_SIZE    (256 * 1024)

/* EBCDIC NEL (0x15) — ASCII LF (0x0A) maps to this in CP037 */
#define EBCDIC_NEL      0x15

/* EBCDIC CR (0x0D) */
#define EBCDIC_CR       0x0D

/* EBCDIC blank */
#define EBCDIC_BLANK    0x40

/* --------------------------------------------------------------------
** Submit JCL from data connection to JES2 internal reader.
**
** Called when sess->filetype == FT_JES and command is STOR.
** JCL is line-based text: each line delimited by CRLF is one card.
** ----------------------------------------------------------------- */
int
ftpd_jes_submit(ftpd_session_t *sess)
{
    VSFILE *intrdr = NULL;
    char *jclbuf = NULL;
    int jcllen;
    char card[81];
    char jobid[9];
    int rc;
    int nread;
    int cardcount;
    char *p;
    char *linestart;
    int linelen;

    /* Reply 125 before opening data connection */
    ftpd_session_reply(sess, FTP_125, "Submitting JCL to internal reader");

    if (ftpd_data_open(sess) != 0) {
        ftpd_session_reply(sess, FTP_425,
                           "Cannot open data connection");
        return 0;
    }

    /* Step 1: Receive ALL JCL from data connection into buffer */
    jclbuf = calloc(1, MAX_JCL_SIZE + 1);
    if (!jclbuf) {
        ftpd_data_close(sess);
        ftpd_session_reply(sess, FTP_451,
                           "Memory allocation failed");
        return 0;
    }

    jcllen = 0;
    while ((nread = ftpd_data_recv(sess, jclbuf + jcllen,
                                   MAX_JCL_SIZE - jcllen)) > 0) {
        jcllen += nread;
        if (jcllen >= MAX_JCL_SIZE) break;
    }
    ftpd_data_close(sess);

    ftpd_log(LOG_INFO, "JES: received %d bytes of JCL", jcllen);

    if (jcllen == 0) {
        free(jclbuf);
        ftpd_session_reply(sess, FTP_550,
                           "No JCL data received");
        return 0;
    }

    /* Step 2: Translate entire buffer ASCII→EBCDIC (CP037).
    ** After translation, ASCII LF (0x0A) becomes EBCDIC NEL (0x15).
    ** ASCII CR (0x0D) stays 0x0D in CP037. */
    if (sess->type == XFER_TYPE_A)
        ftpd_xlat_mvs_a2e((unsigned char *)jclbuf, jcllen);

    /* Open JES2 internal reader */
    rc = jesiropn(&intrdr);
    if (rc < 0) {
        ftpd_log(LOG_ERROR, "JES: jesiropn() failed rc=%d", rc);
        free(jclbuf);
        ftpd_session_reply(sess, FTP_451,
                           "Unable to open JES internal reader");
        return 0;
    }

    /* Step 3: Tokenize at EBCDIC NEL (0x15) and submit each line
    ** as a padded 80-byte card.  Matches mvsMF submit_jcl_content()
    ** lines 1658-1735. */
    cardcount = 0;
    linestart = jclbuf;

    for (p = jclbuf; p < jclbuf + jcllen; p++) {
        if (*p == EBCDIC_NEL) {
            /* Found line delimiter */
            linelen = (int)(p - linestart);

            /* Strip trailing EBCDIC CR if present (CRLF → CR then NEL) */
            if (linelen > 0 && linestart[linelen - 1] == EBCDIC_CR)
                linelen--;

            /* Truncate to 80 */
            if (linelen > 80) linelen = 80;

            /* Build card: copy line + pad with EBCDIC blank (0x40) */
            memset(card, EBCDIC_BLANK, 80);
            if (linelen > 0)
                memcpy(card, linestart, linelen);
            card[80] = '\0';

            wtof("FTPD JES CARD %03d: %.80s", cardcount + 1, card);

            rc = jesirput(intrdr, card);
            if (rc < 0) {
                ftpd_log(LOG_ERROR,
                         "JES: jesirput() failed rc=%d card=%d",
                         rc, cardcount);
                goto fail;
            }
            cardcount++;

            linestart = p + 1;  /* skip past NEL */
        }
    }

    /* Final line without trailing NEL (if any) */
    linelen = (int)(jclbuf + jcllen - linestart);
    if (linelen > 0) {
        if (linestart[linelen - 1] == EBCDIC_CR)
            linelen--;
        if (linelen > 80) linelen = 80;
        if (linelen > 0) {
            memset(card, EBCDIC_BLANK, 80);
            memcpy(card, linestart, linelen);
            card[80] = '\0';

            wtof("FTPD JES CARD %03d: %.80s", cardcount + 1, card);

            rc = jesirput(intrdr, card);
            if (rc < 0) {
                ftpd_log(LOG_ERROR,
                         "JES: jesirput() failed rc=%d (final card)", rc);
                goto fail;
            }
            cardcount++;
        }
    }

    free(jclbuf);
    jclbuf = NULL;

    ftpd_log(LOG_INFO, "JES: closing internal reader, %d cards, %d bytes",
             cardcount, jcllen);

    /* Step 4: Close internal reader — triggers JES2 processing */
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

    ftpd_log(LOG_INFO, "JES: job submitted: %s (%d cards, %d bytes)",
             jobid, cardcount, jcllen);

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

    sess->bytes_recv += jcllen;
    sess->xfer_count++;
    if (sess->server)
        sess->server->total_bytes_in += jcllen;

    return 0;

fail:
    ftpd_data_close(sess);
    if (jclbuf) free(jclbuf);
    if (intrdr) {
        ftpd_log(LOG_ERROR, "JES: emergency closing internal reader");
        jesircls(intrdr);
    }
    ftpd_session_reply(sess, FTP_451,
                       "Failed to submit JCL to internal reader");
    return 0;
}
