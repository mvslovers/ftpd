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

#endif /* FTPD_JES_H */
