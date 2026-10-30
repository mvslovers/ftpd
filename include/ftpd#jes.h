#ifndef FTPD_JES_H
#define FTPD_JES_H
/*
** FTPD JES Interface
**
** Job submission via JES2 internal reader (jesir*() API).
** Job query via JES2 checkpoint (jesopen/jesjob/jesclose).
** Pattern follows mvsmf/src/jobsapi.c.
*/

/*
** Submit JCL received on data connection to JES2 internal reader.
** Active when sess->filetype == FT_JES.
*/
int ftpd_jes_submit(ftpd_session_t *sess)               asm("FTPJESUB");

/*
** List jobs or spool files in JES mode.
** If arg starts with "JOB" → spool file listing for that job.
** Otherwise → job listing with JESOWNER/JESJOBNAME filters.
*/
int ftpd_jes_list(ftpd_session_t *sess, const char *arg) asm("FTPJELST");

/*
** Retrieve spool output for a job.
** RETR JOBnnnnn   → all spool files concatenated.
** RETR JOBnnnnn.n → specific spool file (1-based index).
*/
int ftpd_jes_retrieve(ftpd_session_t *sess, const char *arg)
                                                          asm("FTPJERET");

/*
** Delete (purge) a job from JES queues.
** DELE JOBnnnnn → jescanj(jobname, jobid, 1).
*/
int ftpd_jes_delete(ftpd_session_t *sess, const char *arg)
                                                          asm("FTPJEDEL");

/*
** May the session narrow its JES listing to this SITE JESOWNER value?
** The value is checked whole, in any case.  canon (at least 9 bytes)
** receives what to store: the upper-case value, or "" to reset.
**   FTPD_JES_OWNER_OK      accepted, store canon
**   FTPD_JES_OWNER_BAD     not a userid, "*" or empty: a syntax error (501)
**   FTPD_JES_OWNER_DENIED  well formed, but the session has no READ on the
**                          owner's jobs: ignored, the filter stays (200)
** A denial is logged at debug level and does not count into the session's
** refusal budget (ftpd#jwn.h).
*/
#define FTPD_JES_OWNER_OK       1
#define FTPD_JES_OWNER_BAD      0
#define FTPD_JES_OWNER_DENIED  (-1)

int ftpd_jes_owner_ok(ftpd_session_t *sess, const char *value,
                      char *canon, int canonsz)           asm("FTPJEOWN");

#endif /* FTPD_JES_H */
