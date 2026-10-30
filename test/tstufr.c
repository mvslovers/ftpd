/* TSTUFR.C
** Test for ftpd_ufs_retr() -- #313: a read error during RETR of a UFS file
** must not be answered 226.
**
** ufs_fread() returns 0 both at end of file and after a failure; only the
** error flag on the handle (ufs_ferror()) tells them apart.  RETR looked at
** the count alone, closed the data connection and sent 226, so the client
** kept a truncated file as a complete one.  STOR has checked ufs_ferror()
** since #149.
**
** The libufs calls RETR makes -- ufs_fopen(), ufs_fread(), ufs_ferror(),
** ufs_fclose() -- are replaced by scripted ones (a read error cannot be made
** to happen on demand with a real UFSD), and so are the reply and data
** connection functions.  ftpd#ufs.c is part of this translation unit so that
** only those four calls are redirected.
**
** Rounds, each against the real RETR code:
**   1  clean transfer                           226
**   2  error after full chunks, read returns 0  451
**   3  error on a read that returned a partial  451, the partial was sent
**   4  ufs_ferror() = -1 (request never sent)   451
**   5  data connection lost while sending       426
**   6  read error and lost connection           426
**   7  data connection refused (existing path)  425
** In every round the handle is closed once, cur_ufs_file is cleared and the
** data connection is closed.  A transfer that failed is not counted.
**
** MVS only: ftpd#ufs.c includes ftpd.h and so the libc370 MVS headers.
*/
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "ftpd.h"
#include "ftpd#ses.h"
#include "ftpd#dat.h"
#include "ftpd#ufs.h"
#include "ftpd#urc.h"
#include "libufs.h"
#include <mbtcheck.h>

#define CHUNK       FTPD_DATA_BUF_SIZE

static ftpd_server_t    server;
static ftpd_session_t   sess;

/* The script for the replaced libufs calls. */
static int      reads_ok;       /* full chunks ufs_fread() returns         */
static int      end_error;      /* the read after them fails, not EOF      */
static UINT32   partial;        /* bytes that failing read still delivers  */
static int      err_rc;         /* what ufs_ferror() reports once failed   */
static int      read_calls;
static int      failed;         /* the error flag is set on the handle     */

static int      send_fail_call; /* 1-based send that fails; 0 = none       */
static int      send_calls;
static long     sent_bytes;
static int      data_open_rc;
static int      data_closes;

static int      close_calls;
static UFSFILE *close_fp;
static int      last_code;
static char     last_text[200];

static char     handle[16];     /* what the replaced ufs_fopen() hands out */
static char     ufs_dummy[16];

int
ftpd_session_reply(ftpd_session_t *s, int code, const char *fmt, ...)
{
    va_list ap;

    (void)s;
    last_code = code;
    va_start(ap, fmt);
    vsnprintf(last_text, sizeof(last_text), fmt, ap);
    va_end(ap);
    return 0;
}

int ftpd_data_open(ftpd_session_t *s)  { (void)s; return data_open_rc; }
void ftpd_data_close(ftpd_session_t *s) { (void)s; data_closes++; }
int ftpd_data_recv(ftpd_session_t *s, void *b, int n)
{
    (void)s; (void)b; (void)n;
    return 0;
}
int ftpd_data_printf(ftpd_session_t *s, const char *fmt, ...)
{
    (void)s; (void)fmt;
    return 0;
}
int
ftpd_data_send(ftpd_session_t *s, const void *b, int n)
{
    (void)s; (void)b;
    send_calls++;
    if (send_fail_call && send_calls == send_fail_call)
        return -1;
    sent_bytes += n;
    return n;
}

static UFSFILE *
tstufr_fopen(UFS *ufs, const char *path, const char *mode)
{
    (void)ufs; (void)path; (void)mode;
    return (UFSFILE *)handle;
}

static UINT32
tstufr_fread(void *ptr, UINT32 size, UINT32 nitems, UFSFILE *fp)
{
    UINT32 want = size * nitems;
    UINT32 n;

    (void)fp;
    if (failed)                         /* the flag is sticky, as in libufs */
        return 0;
    read_calls++;
    if (read_calls <= reads_ok) {
        n = want;
    } else if (end_error) {
        failed = 1;
        n = partial < want ? partial : want;
    } else {
        return 0;
    }
    memset(ptr, 'A', n);
    return n;
}

static INT32
tstufr_ferror(UFSFILE *fp)
{
    (void)fp;
    return failed ? err_rc : 0;
}

static void
tstufr_fclose(UFSFILE **pfp)
{
    close_calls++;
    close_fp = *pfp;
    *pfp = NULL;
}

#define ufs_fopen   tstufr_fopen
#define ufs_fread   tstufr_fread
#define ufs_ferror  tstufr_ferror
#define ufs_fclose  tstufr_fclose
#include "../src/ftpd#ufs.c"
#undef ufs_fopen
#undef ufs_fread
#undef ufs_ferror
#undef ufs_fclose

static void
fresh(void)
{
    memset(&server, 0, sizeof(server));
    memset(&sess, 0, sizeof(sess));
    sess.server = &server;
    sess.fsmode = FS_UFS;
    sess.type = XFER_TYPE_I;
    sess.ufs = (UFS *)ufs_dummy;
    sess.data_sock = -1;
    sess.pasv_sock = -1;
    strcpy(sess.ufs_cwd, "/");
    strcpy(sess.user, "IBMUSER");

    reads_ok = 0;
    end_error = 0;
    partial = 0;
    err_rc = 0;
    read_calls = 0;
    failed = 0;
    send_fail_call = 0;
    send_calls = 0;
    sent_bytes = 0;
    data_open_rc = 0;
    data_closes = 0;
    close_calls = 0;
    close_fp = NULL;
    last_code = 0;
    last_text[0] = '\0';
}

/* The checks every round shares: the handle is closed once, recovery has
** nothing left to release, the data connection is closed. */
static void
cleaned_up(const char *round)
{
    CHECK(close_calls == 1, round);
    CHECK(close_fp == (UFSFILE *)handle, round);
    CHECK(sess.cur_ufs_file == NULL, round);
    CHECK(data_closes >= 1, round);
}

int
main(void)
{
    printf("=== FTPD UFS RETR read error tests (#313) ===\n");

    /* 1: a clean transfer is still 226 and counted. */
    fresh();
    reads_ok = 2;
    ftpd_ufs_retr(&sess, "/tmp/f");
    CHECK(last_code == FTP_226, "round 1: clean transfer is 226");
    CHECK(sent_bytes == 2L * CHUNK, "round 1: both chunks were sent");
    CHECK(sess.xfer_count == 1, "round 1: the transfer is counted");
    cleaned_up("round 1: handle closed, recovery clear, data closed");

    /* 2: the read after the first chunks fails and returns 0. */
    fresh();
    reads_ok = 2;
    end_error = 1;
    err_rc = UFSD_RC_IO;
    ftpd_ufs_retr(&sess, "/tmp/f");
    CHECK(last_code == FTP_451, "round 2: a read error is 451, not 226");
    CHECK(sent_bytes == 2L * CHUNK, "round 2: what was read was sent");
    CHECK(sess.xfer_count == 0, "round 2: the transfer is not counted");
    CHECK(strstr(last_text, "incomplete") != NULL,
          "round 2: the text says the file is incomplete");
    cleaned_up("round 2: handle closed, recovery clear, data closed");

    /* 3: libufs delivered part of a chunk before the request failed. */
    fresh();
    reads_ok = 1;
    end_error = 1;
    partial = 100;
    err_rc = UFSD_RC_IO;
    ftpd_ufs_retr(&sess, "/tmp/f");
    CHECK(last_code == FTP_451, "round 3: a partial read then error is 451");
    CHECK(sent_bytes == (long)CHUNK + 100, "round 3: the partial was sent");
    cleaned_up("round 3: handle closed, recovery clear, data closed");

    /* 4: -1 is what libufs reports when the request never reached UFSD. */
    fresh();
    reads_ok = 0;
    end_error = 1;
    err_rc = -1;
    ftpd_ufs_retr(&sess, "/tmp/f");
    CHECK(last_code == FTP_451, "round 4: ferror -1 is 451, not 226");
    cleaned_up("round 4: handle closed, recovery clear, data closed");

    /* 5: the client went away; the file read fine. */
    fresh();
    reads_ok = 5;
    send_fail_call = 2;
    ftpd_ufs_retr(&sess, "/tmp/f");
    CHECK(last_code == FTP_426,
          "round 5: a lost data connection is 426, not 226");
    CHECK(sess.xfer_count == 0, "round 5: the transfer is not counted");
    cleaned_up("round 5: handle closed, recovery clear, data closed");

    /* 6: both at once: the connection is what the client sees first. */
    fresh();
    reads_ok = 1;
    end_error = 1;
    partial = 100;
    err_rc = UFSD_RC_IO;
    send_fail_call = 2;
    ftpd_ufs_retr(&sess, "/tmp/f");
    CHECK(last_code == FTP_426, "round 6: lost connection wins: 426");
    cleaned_up("round 6: handle closed, recovery clear, data closed");

    /* 7: refused data connection is unchanged. */
    fresh();
    data_open_rc = -1;
    ftpd_ufs_retr(&sess, "/tmp/f");
    CHECK(last_code == FTP_425, "round 7: 425 Cannot open data connection");
    CHECK(close_calls == 1 && sess.cur_ufs_file == NULL,
          "round 7: handle closed, recovery clear");

    return mbt_test_summary("TSTUFR");
}
