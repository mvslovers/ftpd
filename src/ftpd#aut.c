/*
** FTPD Authentication
**
** RAKF-based authentication via crent370 racf module.
** Verifies userid/password and checks FACILITY class FTPAUTH.
*/
#include "ftpd.h"
#include "ftpd#ses.h"
#include "ftpd#aut.h"
#include "cliblock.h"               /* lock()/unlock() — identity window */

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
    int auth_rc;
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
    auth_rc = racf_auth(acee, FTPD_FACILITY_CLASS, FTPD_FACILITY_RESOURCE,
                        RACF_ATTR_READ);
    if (auth_rc == FTPD_RACF_NOTPROT)
        ftpd_log(LOG_DEBUG, "%s: %s.%s has no profile — FTP access is open",
                 __func__, FTPD_FACILITY_CLASS, FTPD_FACILITY_RESOURCE);
    if (!ftpd_racf_allowed(auth_rc)) {
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

    sess->authenticated = 1;
    sess->auth_attempts = 0;

    /* Set working directory to user's HLQ */
    strcpy(sess->hlq, user);
    strcat(sess->hlq, ".");
    strcpy(sess->mvs_cwd, sess->hlq);

    /* Also store uppercased userid and password (for JES USER= injection) */
    strcpy(sess->user, user);
    {
        int i;
        for (i = 0; i < 8 && password[i]; i++)
            sess->pass[i] = (char)toupper((unsigned char)password[i]);
        sess->pass[i] = '\0';
    }

    sess->state = SESS_READY;

    ftpd_session_reply(sess, FTP_230,
        "%s is logged on.  Working directory is \"%s\".",
        sess->user, sess->hlq);

    ftpd_log(LOG_INFO, "%s: %s logged in", __func__, user);

    return 0;
}

/* --------------------------------------------------------------------
** Open this session's identity window: take the address-space-wide ENQ,
** then switch ASXBSENV to the session's ACEE.
**
** No-op for an unauthenticated session — with no ACEE there is nothing to
** switch, so there is nothing to serialize either.
**
** lock() waits rather than failing, which is why enter() has no error
** return and no call site needs a failure path.  That is safe only while
** the no-open-data-set invariant in ftpd#aut.h holds; read it before adding
** a window.  RC=8 means this task already holds the ENQ, i.e. two windows
** nested — a coding error the ENQ cannot express, so say so loudly.
** ----------------------------------------------------------------- */
void
ftpd_acee_enter(ftpd_session_t *sess)
{
    if (!sess->acee)
        return;

    if (lock(&sess->server->acee_lock, LOCK_EXC) == 8)
        ftpd_log(LOG_ERROR, "%s: identity window already open on this task "
                 "— nested enter, window exclusivity is broken", __func__);

    racf_set_acee(sess->acee);
}

/* --------------------------------------------------------------------
** Close the window: restore the STC identity captured at startup
** (ftpd.c: server->stc_acee), then release the ENQ.
**
** ORDER IS LOAD-BEARING.  Releasing first would let the next window's owner
** set its own ACEE, and this task would then overwrite it with the STC
** identity — handing a live window the wrong identity, which is the very
** failure the ENQ exists to prevent.  ABEND recovery resets and releases in
** the same order, for the same reason.
**
** stc_acee may be NULL if the startup RACINIT failed — that is still the
** correct value to restore, and is the same value ABEND recovery writes.
** ----------------------------------------------------------------- */
void
ftpd_acee_leave(ftpd_session_t *sess)
{
    if (!sess->acee)
        return;

    racf_set_acee(sess->server->stc_acee);
    unlock(&sess->server->acee_lock, LOCK_EXC);
}

/* --------------------------------------------------------------------
** Does this racf_auth() return code allow the access?
**
** 0 = a profile permits it, 4 = no profile covers the resource.  Both are
** SAF "allowed"; 8 and up are refusals.  See ftpd#aut.h for why testing
** rc != 0 alone survived this long.
** ----------------------------------------------------------------- */
int
ftpd_racf_allowed(int rc)
{
    return (rc == 0 || rc == FTPD_RACF_NOTPROT);
}
