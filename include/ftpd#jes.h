#ifndef FTPD_JES_H
#define FTPD_JES_H
/*
** FTPD JES Interface
**
** Job submission via JES2 internal reader using crent370 jesir*() API.
** Pattern follows mvsmf/src/jobsapi.c submit_jcl_content().
*/

/*
** Submit JCL received on data connection to JES2 internal reader.
** Active when sess->filetype == FT_JES.
** Returns 0 on success, -1 on error.
*/
int ftpd_jes_submit(ftpd_session_t *sess)               asm("FTPJESUB");

#endif /* FTPD_JES_H */
