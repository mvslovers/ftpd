/*
** FTPD UFS Operations — UFSD Client Integration
**
** Wrapper layer around libufs (UFSD client library).
** Provides: session handle management, error mapping,
** path resolution, and CWD/CDUP for UFS mode.
**
** Reference implementation: mvsmf/src/ussapi.c
**
** Codepage: UFS files are IBM-1047 (NOT CP037).
** Use ftpd_xlat_a2e/e2a for UFS content translation.
*/
#include "ftpd.h"
#include "ftpd#ses.h"
#include "ftpd#ufs.h"

#include "libufs.h"

/* --------------------------------------------------------------------
** UFSD RC -> FTP reply code mapping
** ----------------------------------------------------------------- */
int
ftpd_ufs_rc_to_ftp(int rc)
{
    switch (rc) {
    case UFSD_RC_OK:          return FTP_250;
    case UFSD_RC_NOFILE:      return FTP_550;
    case UFSD_RC_EXIST:       return FTP_550;
    case UFSD_RC_NOTDIR:      return FTP_550;
    case UFSD_RC_ISDIR:       return FTP_550;
    case UFSD_RC_NOSPACE:     return FTP_452;
    case UFSD_RC_NOINODES:    return FTP_452;
    case UFSD_RC_IO:          return FTP_451;
    case UFSD_RC_BADFD:       return FTP_451;
    case UFSD_RC_NOTEMPTY:    return FTP_550;
    case UFSD_RC_NAMETOOLONG: return FTP_553;
    case UFSD_RC_ROFS:        return FTP_550;
    case UFSD_RC_EACCES:      return FTP_550;
    default:                  return FTP_451;
    }
}

/* --------------------------------------------------------------------
** UFSD RC -> human-readable message
** ----------------------------------------------------------------- */
const char *
ftpd_ufs_rc_message(int rc)
{
    switch (rc) {
    case UFSD_RC_OK:          return "Success";
    case UFSD_RC_NOFILE:      return "File or directory not found";
    case UFSD_RC_EXIST:       return "File or directory already exists";
    case UFSD_RC_NOTDIR:      return "Not a directory";
    case UFSD_RC_ISDIR:       return "Is a directory";
    case UFSD_RC_NOSPACE:     return "No space left on device";
    case UFSD_RC_NOINODES:    return "No inodes available";
    case UFSD_RC_IO:          return "I/O error";
    case UFSD_RC_BADFD:       return "Bad file descriptor";
    case UFSD_RC_NOTEMPTY:    return "Directory not empty";
    case UFSD_RC_NAMETOOLONG: return "Path name too long";
    case UFSD_RC_ROFS:        return "Read-only file system";
    case UFSD_RC_EACCES:      return "Permission denied";
    default:                  return "UFS error";
    }
}

/* --------------------------------------------------------------------
** Send FTP error reply for a UFSD return code
** ----------------------------------------------------------------- */
void
ftpd_ufs_error(ftpd_session_t *sess, int ufsd_rc)
{
    ftpd_session_reply(sess, ftpd_ufs_rc_to_ftp(ufsd_rc),
                       "%s", ftpd_ufs_rc_message(ufsd_rc));
}

/* --------------------------------------------------------------------
** Get per-session UFS handle (lazy init)
**
** Pattern follows mvsMF uss_get_ufs():
**   1. ufsnew() — creates UFSD session via SVC 34
**   2. ufs_setuser() — sets owner from ACEE for permission checks
**
** Returns NULL if UFSD is not running (sends 550 reply).
** Caches handle in sess->ufs for subsequent calls.
** ----------------------------------------------------------------- */
UFS *
ftpd_ufs_get(ftpd_session_t *sess)
{
    UFS *ufs;

    // Return cached handle if available
    if (sess->ufs)
        return sess->ufs;

    // Try to connect to UFSD
    ufs = ufsnew();
    if (!ufs) {
        ftpd_session_reply(sess, FTP_550,
                           "UFS service not available");
        return NULL;
    }

    // Set session owner from ACEE (exactly like mvsMF uss_get_ufs)
    if (sess->acee) {
        ACEE *acee = sess->acee;
        char userid[9];
        char group[9];
        unsigned char ulen = (unsigned char)acee->aceeuser[0];
        unsigned char glen = (unsigned char)acee->aceegrp[0];
        if (ulen > 8) ulen = 8;
        if (glen > 8) glen = 8;
        memset(userid, 0, sizeof(userid));
        memset(group, 0, sizeof(group));
        memcpy(userid, acee->aceeuser + 1, ulen);
        memcpy(group, acee->aceegrp + 1, glen);
        ufs_setuser(ufs, userid, group);
    }

    sess->ufs = ufs;
    return ufs;
}

/* --------------------------------------------------------------------
** Free per-session UFS handle
** ----------------------------------------------------------------- */
void
ftpd_ufs_free(ftpd_session_t *sess)
{
    if (sess->ufs) {
        ufsfree(&sess->ufs);
        sess->ufs = NULL;
    }
}

/* --------------------------------------------------------------------
** Resolve path argument against session's UFS CWD.
**
** Rules:
**   - Absolute path (/...) → use as-is
**   - "." → current directory
**   - ".." → parent directory
**   - relative → append to cwd
**   - Normalize: collapse "//", "/./" and "/../"
**   - Prevent traversal above root "/"
**
** Returns 0 on success, -1 on error (path too long, invalid).
** ----------------------------------------------------------------- */
int
ftpd_ufs_resolve(ftpd_session_t *sess, const char *arg,
                 char *out, int outlen)
{
    char work[FTPD_MAX_PATH_LEN];
    char *src;
    char *dst;
    char *seg;
    char *next;
    int len;

    if (!arg || !arg[0]) {
        // No argument — return current directory
        strncpy(out, sess->ufs_cwd, outlen - 1);
        out[outlen - 1] = '\0';
        return 0;
    }

    // Build absolute path in work buffer
    if (arg[0] == '/') {
        // Absolute path
        strncpy(work, arg, sizeof(work) - 1);
        work[sizeof(work) - 1] = '\0';
    } else {
        // Relative to CWD
        len = snprintf(work, sizeof(work), "%s/%s", sess->ufs_cwd, arg);
        if (len >= (int)sizeof(work))
            return -1;
    }

    // Normalize: resolve . and .. components
    // Output always starts with /
    dst = out;
    *dst = '\0';

    src = work;
    if (*src == '/')
        src++;

    // Start with root
    if (outlen < 2)
        return -1;
    dst[0] = '/';
    dst[1] = '\0';
    len = 1;

    // Process each path segment
    seg = src;
    while (*seg) {
        // Find end of segment
        next = seg;
        while (*next && *next != '/')
            next++;

        // Temporarily null-terminate segment
        if (*next == '/') {
            *next = '\0';
            next++;
        }

        // Skip empty segments and "."
        if (seg[0] == '\0' || strcmp(seg, ".") == 0) {
            seg = next;
            continue;
        }

        // Handle ".." — go up one level
        if (strcmp(seg, "..") == 0) {
            // Find last slash (not the trailing one)
            char *last = out + len - 1;
            if (len > 1) {
                // Remove trailing slash if present
                if (*last == '/')
                    last--;
                // Find previous slash
                while (last > out && *last != '/')
                    last--;
                len = last - out + 1;
                out[len] = '\0';
            }
            // At root — stay at root (prevent traversal)
            seg = next;
            continue;
        }

        // Append segment
        if (len > 1) {
            // Need slash separator (unless we're at root "/")
            if (len + 1 >= outlen)
                return -1;
            out[len++] = '/';
        }
        if (len + (int)strlen(seg) >= outlen)
            return -1;
        strcpy(out + len, seg);
        len += strlen(seg);

        seg = next;
    }

    // Ensure at least "/"
    if (len == 0) {
        out[0] = '/';
        out[1] = '\0';
    }

    return 0;
}

/* --------------------------------------------------------------------
** CWD — change UFS working directory
**
** Validates the target path via ufs_chgdir().
** On success, updates sess->ufs_cwd from the UFSD CWD.
** ----------------------------------------------------------------- */
int
ftpd_ufs_cwd(ftpd_session_t *sess, const char *arg)
{
    UFS *ufs;
    char path[FTPD_MAX_PATH_LEN];
    int rc;
    UFSCWD *cwd;

    ufs = ftpd_ufs_get(sess);
    if (!ufs)
        return 0;

    // Resolve the target path
    if (ftpd_ufs_resolve(sess, arg, path, sizeof(path)) != 0) {
        ftpd_session_reply(sess, FTP_550, "Invalid path");
        return 0;
    }

    // Change directory via UFSD
    rc = ufs_chgdir(ufs, path);
    if (rc != UFSD_RC_OK) {
        ftpd_ufs_error(sess, rc);
        return 0;
    }

    // Update session CWD from UFSD
    cwd = ufs_get_cwd(ufs);
    if (cwd && cwd->path[0]) {
        strncpy(sess->ufs_cwd, cwd->path, sizeof(sess->ufs_cwd) - 1);
        sess->ufs_cwd[sizeof(sess->ufs_cwd) - 1] = '\0';
    } else {
        strncpy(sess->ufs_cwd, path, sizeof(sess->ufs_cwd) - 1);
        sess->ufs_cwd[sizeof(sess->ufs_cwd) - 1] = '\0';
    }

    ftpd_session_reply(sess, FTP_250,
                       "HFS directory %s is the current working directory",
                       sess->ufs_cwd);
    return 0;
}

/* --------------------------------------------------------------------
** CDUP — move to parent UFS directory
** ----------------------------------------------------------------- */
int
ftpd_ufs_cdup(ftpd_session_t *sess)
{
    return ftpd_ufs_cwd(sess, "..");
}
