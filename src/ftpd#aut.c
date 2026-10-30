/*
** FTPD Authentication
**
** RAKF-based authentication via libc370 racf module.
** Verifies userid/password and checks FACILITY class FTPAUTH.
*/
#include "ftpd.h"
#include "ftpd#ses.h"
#include "ftpd#aut.h"
#include "ftpd#anm.h"
#include <mvs/lock.h>               /* lock()/unlock() — identity window */

#define FTPD_MAX_AUTH_ATTEMPTS  3
#define FTPD_FACILITY_RESOURCE  "FTPAUTH"
#define FTPD_FACILITY_CLASS     "FACILITY"

/* A userid no security system defines ('?' is not a RACF userid character)
** and a password to go with it, for the start check. */
#define FTPD_PROBE_USER         "?FTPDPRB"
#define FTPD_PROBE_PASS         "?NOPASS?"

/* --------------------------------------------------------------------
** Is the ACEE racf_login() returned the one of the user who logged in?
**
** The ACEE has to belong to the user who was asked for, and it must not be
** the started task's own: that one would be freed from under the address
** space by the racf_logout() below.  Otherwise the login is refused.  The
** STC ACEE is never freed: it is the resting identity the server restores
** after every identity window.
**
** Returns the ACEE to use, or NULL after refusing it.
** ----------------------------------------------------------------- */
static ACEE *
login_acee_ok(ftpd_server_t *server, ACEE *acee, const char *user)
{
    char name[9];
    int  rc;

    if (acee != server->stc_acee
        && ftpd_acee_user_matches(acee->aceeuser, user))
        return acee;

    ftpd_acee_user_name(name, acee->aceeuser);
    ftpd_log(LOG_WARN, "%s: RAKF login for %s returned the ACEE of %s%s, "
             "refused", __func__, user, name[0] ? name : "nobody",
             acee == server->stc_acee ? " (the started task's own)" : "");

    if (acee != server->stc_acee) {
        rc = racf_logout(&acee);
        if (rc != 0)
            ftpd_log(LOG_WARN, "%s: racf_logout of the refused ACEE "
                     "failed, rc=%d", __func__, rc);
    }
    return NULL;
}

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

    /* Without program authorization racf_login() cannot fail politely: it
    ** MODESETs into supervisor state and the task ABENDs S047, so nothing
    ** below this point is reachable.  Say so instead.  This is neither a
    ** wrong password nor a reason to close the session, so the attempt is
    ** not counted and the client may go on with USER, SYST, NOOP or QUIT.
    ** Logged once per server: the operator has FTPD003W, and a client can
    ** send PASS as often as it likes. */
    if (!sess->server->authorized) {
        if (!sess->server->unauth_logged) {
            sess->server->unauth_logged = 1;
            ftpd_log(LOG_ERROR, "%s: login refused, FTPD is not APF "
                     "authorized (see FTPD003W); no user can be logged in",
                     __func__);
        }
        ftpd_session_reply(sess, FTP_530,
            "Login not possible: FTPD is not APF authorized. "
            "Ask the administrator to check the FTPD003W console message.");
        sess->state = SESS_AUTH_USER;
        return 0;
    }

    /* The start check of the security system failed, so its answer to a
    ** login cannot be relied on and nobody logs in.  Like the unauthorized
    ** case this is not a wrong password and not counted; the operator has
    ** FTPD004E. */
    if (sess->server->open_security) {
        if (!sess->server->open_logged) {
            sess->server->open_logged = 1;
            ftpd_log(LOG_ERROR, "%s: login refused, the start check of "
                     "the security system failed (see FTPD004E)", __func__);
        }
        ftpd_session_reply(sess, FTP_530,
            "Login not possible: the security check of this server is not "
            "working. Ask the administrator to check the FTPD004E console "
            "message.");
        sess->state = SESS_AUTH_USER;
        return 0;
    }

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

    if (acee)
        acee = login_acee_ok(sess->server, acee, user);

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
        ftpd_log(LOG_DEBUG, "%s: %s.%s has no profile - FTP access is open",
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
                 "- nested enter, window exclusivity is broken", __func__);

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
** The identity is the one RAKF gave the STC, never NULL on a system that
** has a security environment (FTPD004W otherwise).  It is also the value
** ABEND recovery writes.
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

/* --------------------------------------------------------------------
** Start check: does the security system verify userids?
** See ftpd#aut.h.
** ----------------------------------------------------------------- */
int
ftpd_auth_probe(ftpd_server_t *server)
{
    ACEE *acee;
    int racf_rc = -1;
    int rc;

    if (!server->authorized)
        return 0;

    acee = racf_login(FTPD_PROBE_USER, FTPD_PROBE_PASS, NULL, &racf_rc);

    if (!acee && racf_rc != 0) {
        ftpd_log(LOG_DEBUG, "%s: the security system refused the check "
                 "login, rc=%d", __func__, racf_rc);
        return 0;
    }

    ftpd_log(LOG_ERROR, "%s: the check login was accepted, rc=%d", __func__, racf_rc);
    server->open_security = 1;

    if (acee && acee != server->stc_acee) {
        rc = racf_logout(&acee);
        if (rc != 0)
            ftpd_log(LOG_WARN, "%s: racf_logout of the check ACEE "
                     "failed, rc=%d", __func__, rc);
    }
    return 1;
}
