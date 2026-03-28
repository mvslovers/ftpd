/*
** FTPD JES Interface — Job Submission
**
** Submits JCL received on the FTP data connection to JES2 via the
** internal reader API (jesiropn/jesirput/jesircls).
**
** Pattern follows mvsmf/src/jobsapi.c submit_jcl_content() exactly:
** 1. Open internal reader via jesiropn()
** 2. Receive JCL from data connection, accumulate 80-byte records
** 3. TYPE A: translate ASCII→EBCDIC (CP037) before writing
** 4. Write each 80-byte record via jesirput()
** 5. Close internal reader via jesircls() — triggers JES2 processing
** 6. Extract job ID from intrdr->rpl.rplrbar
** 7. Reply: 250-It is known to JES as JOBnnnnn
*/
#include "ftpd.h"
#include "ftpd#ses.h"
#include "ftpd#dat.h"
#include "ftpd#jes.h"
#include "ftpd#xlt.h"
#include "clibjes2.h"

/* --------------------------------------------------------------------
** Submit JCL from data connection to JES2 internal reader.
**
** Called when sess->filetype == FT_JES and command is STOR.
** Follows the mvsmf jobsapi.c pattern character-for-character.
** ----------------------------------------------------------------- */
int
ftpd_jes_submit(ftpd_session_t *sess)
{
    VSFILE *intrdr = NULL;
    char card[81];
    char jobid[9];
    int cardpos;
    int rc;
    int nread;
    int cardcount;
    long total;

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

    /* Receive JCL from data connection, accumulate 80-byte records.
    ** recv directly into card buffer, limited to space remaining.
    ** This is the exact mvsMF dsapi.c pattern for record accumulation. */
    cardpos = 0;
    cardcount = 0;
    total = 0;
    memset(card, ' ', 80);
    card[80] = '\0';

    while (1) {
        int space = 80 - cardpos;
        nread = ftpd_data_recv(sess, card + cardpos, space);
        if (nread <= 0) break;
        total += nread;
        cardpos += nread;

        if (cardpos >= 80) {
            /* TYPE A: translate ASCII→EBCDIC (CP037) */
            if (sess->type == XFER_TYPE_A)
                ftpd_xlat_mvs_a2e((unsigned char *)card, 80);

            rc = jesirput(intrdr, card);
            if (rc < 0) {
                ftpd_log(LOG_ERROR,
                         "JES: jesirput() failed rc=%d card=%d",
                         rc, cardcount);
                goto fail;
            }
            cardcount++;
            cardpos = 0;
            memset(card, ' ', 80);
        }
    }

    /* Final partial card: pad with EBCDIC blanks and submit */
    if (cardpos > 0) {
        /* Pad remaining bytes with blanks */
        if (sess->type == XFER_TYPE_A) {
            /* Translate the data portion, rest is already blank (0x20 ASCII) */
            ftpd_xlat_mvs_a2e((unsigned char *)card, 80);
        } else {
            /* Binary/EBCDIC: pad with EBCDIC blank (0x40) */
            memset(card + cardpos, 0x40, 80 - cardpos);
        }

        rc = jesirput(intrdr, card);
        if (rc < 0) {
            ftpd_log(LOG_ERROR,
                     "JES: jesirput() failed rc=%d (final card)", rc);
            goto fail;
        }
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
