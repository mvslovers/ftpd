/*
** FTPD Authentication
**
** RAKF-based authentication via crent370 racf module.
** Verifies userid/password and checks FACILITY class FTPAUTH.
** When INSECURE=1, authentication is bypassed (any password accepted).
*/
#include "ftpd.h"
#include "ftpd#ses.h"
#include "ftpd#aut.h"

#define FTPD_MAX_AUTH_ATTEMPTS  3
#define FTPD_FACILITY_RESOURCE  "FTPAUTH"
#define FTPD_FACILITY_CLASS     "FACILITY"

/* --------------------------------------------------------------------
** Authenticate via RAKF and set up session.
** ----------------------------------------------------------------- */
int
ftpd_auth_pass(ftpd_session_t *sess, const char *password)
{
    ACEE *acee;
    int racf_rc;
    char user[9];
    char pass[9];

    /* Uppercase userid and password (RAKF requires uppercase) */
    {
        int i;
        for (i = 0; i < 8 && sess->user[i]; i++)
            user[i] = (char)toupper((unsigned char)sess->user[i]);
        user[i] = '\0';

        for (i = 0; i < 8 && password[i]; i++)
            pass[i] = (char)toupper((unsigned char)password[i]);
        pass[i] = '\0';
    }

    /* INSECURE mode: skip RAKF, accept any password */
    if (sess->server->config.insecure) {
        ftpd_log(LOG_WARN, "%s: INSECURE mode, skipping RAKF for %s",
                 __func__, user);
        goto accept;
    }

    /* Verify credentials via RAKF */
    racf_rc = 0;
    acee = racf_login(user, pass, NULL, &racf_rc);

    /* Clear password from stack immediately */
    memset(pass, 0, sizeof(pass));

    if (!acee) {
        sess->auth_attempts++;
        ftpd_log(LOG_WARN, "%s: RAKF login failed for %s, rc=%d "
                 "(attempt %d/%d)", __func__, user, racf_rc,
                 sess->auth_attempts, FTPD_MAX_AUTH_ATTEMPTS);

        if (sess->auth_attempts >= FTPD_MAX_AUTH_ATTEMPTS) {
            ftpd_session_reply(sess, FTP_530,
                "Login incorrect. Too many attempts, disconnecting.");
            return -1;
        }

        ftpd_session_reply(sess, FTP_530, "Login incorrect.");
        sess->state = SESS_AUTH_USER;
        return 0;
    }

    /* Check FACILITY class authorization */
    if (racf_auth(acee, FTPD_FACILITY_CLASS, FTPD_FACILITY_RESOURCE,
                  RACF_ATTR_READ) != 0) {
        ftpd_log(LOG_WARN, "%s: %s not authorized for %s.%s",
                 __func__, user, FTPD_FACILITY_CLASS,
                 FTPD_FACILITY_RESOURCE);
        racf_logout(&acee);

        sess->auth_attempts++;
        if (sess->auth_attempts >= FTPD_MAX_AUTH_ATTEMPTS) {
            ftpd_session_reply(sess, FTP_530,
                "Not authorized for FTP access. Disconnecting.");
            return -1;
        }

        ftpd_session_reply(sess, FTP_530,
            "Not authorized for FTP access.");
        sess->state = SESS_AUTH_USER;
        return 0;
    }

    /* Store ACEE in session */
    sess->acee = acee;

accept:
    /* Clear password from stack */
    memset(pass, 0, sizeof(pass));

    sess->authenticated = 1;
    sess->auth_attempts = 0;

    /* Set working directory to user's HLQ */
    strcpy(sess->hlq, user);
    strcat(sess->hlq, ".");
    strcpy(sess->mvs_cwd, sess->hlq);

    /* Also store uppercased userid */
    strcpy(sess->user, user);

    sess->state = SESS_READY;

    ftpd_session_reply(sess, FTP_230,
        "%s is logged on.  Working directory is \"%s\".",
        sess->user, sess->hlq);

    ftpd_log(LOG_INFO, "%s: %s logged in%s", __func__, user,
             sess->server->config.insecure ? " (INSECURE)" : "");

    return 0;
}
