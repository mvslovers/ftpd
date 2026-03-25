//FTPD    PROC
//*********************************************************************
//*  FTPD - FTP Server for MVS 3.8j
//*
//*  Start: /S FTPD       Stop: /P FTPD
//*  Console: /F FTPD,D SESSIONS
//*           /F FTPD,D STATS
//*           /F FTPD,D VERSION
//*           /F FTPD,D CONFIG
//*           /F FTPD,KILL sessid
//*           /F FTPD,TRACE ON|OFF|DUMP
//*********************************************************************
//FTPD    EXEC PGM=FTPD,TIME=1440,REGION=4096K
//STEPLIB  DD  DISP=SHR,DSN=IBMUSER.FTPD.V1R0M0D.LOAD
//STDOUT   DD  SYSOUT=*
//STDERR   DD  SYSOUT=*
