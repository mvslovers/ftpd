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

#endif /* FTPD_JES_H */
