#import "../bookmaster/bookmaster.typ": *

= The JES and UFS Views <ug-other>

#idx("view")
Besides MVS data sets, FTPD gives a client two more views of the host: the
jobs in JES2, and the files in a UFS file system. Both use the same FTP
commands as the data set view, and the commands then do something else. A
session starts in the data set view (@ug-data). This chapter says how to switch
to the other two and what each command does there.

== Jobs in JES2 <ug-other-jes>

#idx("JES")#idx("job", "submit")#idx("FILETYPE")
In the JES view a client submits jobs, lists them, reads their output and
cancels them. FTPD finds the JES2 checkpoint and the spool itself, so the
procedure of the started task (@ug-ops-start) needs no DD statement for them.
Only a site whose JES2 uses another #cmd("DSNPRFX") than #cmd("SYS1"), or whose
checkpoint is neither cataloged nor on an online volume, adds the DD
statements (@ug-install-jes). When FTPD cannot read JES2, every command of the
view ends with #cmd("451 Unable to open JES checkpoint").

A client switches to the view with a #cmd("SITE") command (@ug-site) and back
again with another one:

```
SITE FILETYPE=JES
SITE FILETYPE=SEQ
```

The commands #cmd("STOR"), #cmd("LIST"), #cmd("NLST"), #cmd("RETR") and
#cmd("DELE") go to JES2 as long as the view is on. All other commands work as
before. This holds also when the session was in the UFS view (@ug-other-ufs)
when the client switched. #cmd("SITE FILETYPE=SEQ") returns to the view that
was active before.

=== Submitting a Job

#idx("job", "submit")#idx("internal reader")
#cmd("STOR") sends JCL to the JES2 internal reader. FTPD ignores the file name
of the command. The reply to the command shows the transfer:

- *Start:* #cmd("125 Sending Job to JES internal reader FIXrecfm 80")
- *End:* a reply of two lines, #cmd("250-It is known to JES as JOBnnnnn") and
  #cmd("250 Transfer completed successfully."). The job ID is the one that the
  following commands use.

FTPD reads the JCL line by line. A line is one card of 80 columns. A line that
is longer is cut at column 80, a shorter one is filled with blanks, and a
carriage return at the end of a line is removed. FTPD converts the text to
EBCDIC only in text mode (#cmd("TYPE A")). In binary mode it sends the bytes
as they come, so send JCL as text.

FTPD looks for the JOB statement, the first card that has the word JOB between
blanks. It changes the statement in this way:

- At the end of the JOB statement, FTPD adds a comma and then three
  continuation cards.
- The first card is #cmd("NOTIFY=DUMMY,"). FTPD adds it only if the JOB
  statement has no #cmd("NOTIFY") yet.
- The second card is #cmd("USER=") with the user ID of the session.
- The third card is #cmd("PASSWORD=") with the password of the session.

The job runs under the identity of the user, not under the one of the started
task (@ug-security-jes). FTPD does not look for a #cmd("USER=") or a
#cmd("PASSWORD=") that the JCL has already, so leave them out. FTPD adds the
comma only if the last line of the JOB statement ends before column 71. Keep
that line shorter.

FTPD does not check the JCL. A JCL error shows in the list of the job
(@ug-other-jes), as the text #cmd("JCL ERROR") for the job.

If FTPD cannot hand the JCL over, the replies are
#cmd("451 Unable to open JES internal reader"),
#cmd("451 Failed to submit JCL to internal reader") and
#cmd("451 Failed to close internal reader"). A data connection that cannot be
opened gives #cmd("425 Cannot open data connection").

=== Listing Jobs

#idx("job", "list")
#cmd("LIST") and #cmd("NLST") give the same list of jobs. The command
#cmd("125 List started OK for JESJOBNAME=*, JESSTATUS=ALL and JESOWNER=USERID")
opens it (USERID is the owner filter, below), and the list has one line for each job after this header:

```
JOBNAME  JOBID    OWNER    STATUS CLASS
```

#tab(caption: [The columns of the job list])[
  #table(columns: (1.1in, 1fr),
    [Column], [Meaning],
    [JOBNAME, JOBID, OWNER], [The name, the ID and the user ID of the owner.],
    [STATUS], [#cmd("ACTIVE"), #cmd("INPUT"), #cmd("XMIT"), #cmd("SETUP"),
      #cmd("RECEIVE"), #cmd("OUTPUT") or #cmd("UNKNOWN").],
    [CLASS], [#cmd("STC") for a started task and #cmd("TSU") for a TSO user.
      For a job it is the execution class.],
    [(no title)], [For a job in the status #cmd("OUTPUT") the result: for
      example #cmd("RC=0004"), #cmd("ABEND S0C4"), #cmd("ABEND U0123") or
      #cmd("JCL ERROR").],
  )
]

The list is not what the client asks for with the argument. FTPD takes the
filters from the session:

- *The owner:* #cmd("SITE JESOWNER=name") sets it. Without it the owner is
  the user ID of the session, so a user sees the own jobs. FTPD compares the
  beginning of the owner of each job with the value. #cmd("SITE JESOWNER=*")
  lists the jobs of all users.
- *The job name:* #cmd("SITE JESJOBNAME=name") limits the list to the jobs with
  this name.
- *The status:* #cmd("SITE JESSTATUS=status") limits the list to the jobs in
  one status: #cmd("INPUT"), #cmd("ACTIVE") or #cmd("OUTPUT"). The default is
  #cmd("ALL"), every status. A job in one of the other states of the column
  STATUS (#cmd("XMIT"), #cmd("SETUP"), #cmd("RECEIVE"), #cmd("UNKNOWN")) is
  listed with #cmd("ALL") only. The header #cmd("125") names the filter. Before
  FTPD 1.3.0 the keyword was stored and the list showed every job.

The list has at most 200 jobs. When FTPD reaches the limit, the end of the
list is #cmd("250-JESENTRYLIMIT of 200 reached.  Additional entries not displayed") and #cmd("250 List completed successfully."). Narrow the filters to
see the others.

#note[*A job ID as the argument.* An argument that starts with #cmd("JOB"), in
any case, is read as a job ID and gives the list of the spool files of that job
(below). It is not a filter. To list jobs with a name that starts with
#cmd("JOB"), use #cmd("SITE JESJOBNAME").]

The list of the spool files of a job, #cmd("LIST JOB00042"), starts with
#cmd("125 List started OK for JOB00042") and has this header:

```
JOBNAME  JOBID    STEPNAME PROCSTEP C DDNAME   BYTE-COUNT
```

There is one line for each spool file, with the step, the procedure step, the
output class and the DD name. The byte count is the number of records times
the record length of the file, so it is not exact. A job ID that JES2 does not
know, or a job that #cmd("JESSTATUS") excludes, gives
#cmd("550 No job found with ID JOB00042 for JESSTATUS=ALL") (the text names
the filter). The list includes the
input files of the job (the JCL and the SYSIN data). #cmd("RETR") does not
count them, as the next section shows.

=== Reading the Output of a Job

#idx("job", "output")#idx("spool")
#cmd("RETR") reads the output of a job from the spool:

- *#cmd("RETR JOB00042"):* all output files of the job, one after the other.
  FTPD writes the line #cmd(" !! END OF JES SPOOL FILE !!") after every file.
  The start is #cmd("125 Sending all spool files for requested Jobid").
- *#cmd("RETR JOB00042.2"):* only the second output file. The number counts
  the output files of the job from 1, and it does not count input files. The
  start is #cmd("125 Sending data set") with the name of the file.

FTPD converts the text to ASCII in text mode (#cmd("TYPE A")). A line longer
than 253 characters is cut. An empty output file is left out. The end is
#cmd("250 Transfer completed successfully.").

The error replies are:

#tab(caption: [The error replies of RETR in the JES view])[
  #table(columns: (2.6in, 1fr),
    [Reply], [Cause],
    [#cmd("501 Missing job ID")], [No argument.],
    [#cmd("550 Jobid JOB00042 not found for JESJOBNAME=*, JESSTATUS=ALL, JESOWNER=USERID")],
      [JES2 does not know the job, or the job is not in the status that
      #cmd("JESSTATUS") names. The text shows the filters of the session.],
    [#cmd("550 Spool file 5 not found for Jobid JOB00042, 3 spool file(s) available")],
      [The number after the point is 0 or larger than the number of output
      files. The text says how many files the job has.],
    [#cmd("550 Spool data set for JOB00042 no longer available.")],
      [The job is in the list of JES2, but its spool data set is gone.],
    [#cmd("451 Error reading spool data set for JOB00042.")],
      [The read of the spool failed. What FTPD sent is incomplete.],
    [#cmd("426 Data connection closed, transfer aborted.")],
      [The client closed the data connection.],
  )
]

#note[*The job ID and the status.* #cmd("RETR") and #cmd("DELE") find the job
by its ID. The filters #cmd("JESOWNER") and #cmd("JESJOBNAME") do not apply,
and FTPD does not compare the owner of the job with the user ID of the
session. The 550 text names them, but they do not take part in the search.
#cmd("JESSTATUS") does take part, since FTPD 1.3.0: with
#cmd("SITE JESSTATUS=ACTIVE") the output of a job that has ended cannot be
fetched, and a job that is not active cannot be cancelled. Set
#cmd("JESSTATUS") back to #cmd("ALL") first.]

=== Cancelling a Job

#idx("job", "cancel")
#cmd("DELE JOB00042") cancels the job and removes its output from the spool.
The reply is #cmd("250 Cancel successful"). The errors are:

- #cmd("501 Missing job ID") without an argument.
- #cmd("550 Jobid JOB00042 not found for ...") as for #cmd("RETR").
- #cmd("550 Access denied - cannot cancel JOB00042") when JES2 refuses.
- #cmd("550 Failed to cancel JOB00042 (rc=n)") for any other code of JES2.

== Files in UFS <ug-other-ufs>

#idx("UFS")#idx("UFSD")#idx("HFS")
UFS is the Unix-like file system of the UFSD (ufsd) address space. FTPD shows
it to a client as a tree of directories and files, as an FTP server on Unix
does. FTPD talks to UFSD for this. UFSD is optional: without it, FTPD works
with data sets and JES. The first UFS command of a session then gets
#cmd("550 UFS service not available"), and the session stays in the data set
view.

=== Switching

#idx("CWD", "UFS")
#cmd("CWD") decides the view:

- #cmd("CWD /u/ibmuser") with a leading slash switches to UFS. The reply is
  #cmd("250 HFS directory /u/ibmuser is the current working directory").
- #cmd("CWD 'SYS1.PARMLIB'") with a quoted name switches back to the data set
  view (@ug-data-cwd).
- Any other #cmd("CWD") in the UFS view moves relative to the current UFS
  directory, and #cmd("CDUP") goes to the parent. The path may contain
  #cmd("."), #cmd("..") and double slashes. FTPD resolves them. A path cannot go
  above the root: #cmd("..") at the root stays at the root. A path that is too
  long gets #cmd("550 Invalid path").

#cmd("PWD") shows the view. In UFS the reply is #cmd("257 \"/u/ibmuser\" is the HFS working directory.").

=== Commands

FTPD tells UFSD the user ID and the group of the session, and UFSD applies the
owners and permissions of the file system. A refusal is
#cmd("550 Permission denied").

#tab(caption: [FTP commands in the UFS view])[
  #table(columns: (1.1in, 1fr),
    [Command], [Behaviour],
    [#cmd("LIST")], [#cmd("150 Opening data connection for file list"), then a
      line for each entry in the Unix long format: permissions, link count,
      owner, group, size, date and name. The date is always month, day and
      time, without a year. The entries #cmd(".") and #cmd("..") are not listed.
      The end is #cmd("226 Transfer complete").],
    [#cmd("NLST")], [The same, with the name only.],
    [#cmd("RETR"), #cmd("STOR")], [#cmd("150 Opening data connection for"), the
      path, and #cmd("226 Transfer complete"). #cmd("STOR") replaces a file that
      exists. See below for text mode and for errors.],
    [#cmd("DELE")], [#cmd("250 File deleted").],
    [#cmd("MKD"), #cmd("RMD")], [#cmd("257 \"path\" directory created") and
      #cmd("250 Directory removed"). #cmd("RMD") refuses a directory that is not
      empty.],
    [#cmd("RNFR")], [#cmd("350 File exists, ready for destination").],
    [#cmd("RNTO")], [#cmd("502 Rename not supported for UFS"). A file cannot be
      renamed in UFS with FTP.],
    [#cmd("APPE")], [#cmd("502 APPE not supported for UFS"). Append works for
      data sets only (@ug-data-appe).],
    [#cmd("SIZE")], [#cmd("213") and the size in bytes. In the data set view
      #cmd("SIZE") gets #cmd("502").],
    [#cmd("MDTM")], [#cmd("213") and the time of the last change as
      #cmd("YYYYMMDDHHMMSS"). In the data set view #cmd("MDTM") gets
      #cmd("502").],
  )
]

=== Text and Binary

#idx("TYPE", "UFS")
In text mode (#cmd("TYPE A")) FTPD converts the content between ASCII and
EBCDIC, as it does for data sets. For UFS files the EBCDIC code page is IBM-1047.
For data sets it is IBM-037 (@ug-data-retr). A few characters differ between
the two, for example the square brackets. Use binary mode (#cmd("TYPE I")) for a
file that is not text. FTPD then sends the bytes as they are.

=== Errors When a File Is Written

#idx("552", "UFS")#idx("inode")
The replies to a failed #cmd("STOR") depend on the cause:

#tab(caption: [Errors of STOR in the UFS view])[
  #table(columns: (1.6in, 1fr),
    [Reply], [Cause],
    [#cmd("552 No inodes available")],
      [Since FTPD 1.3.0. The file system has no free inode and cannot create
      the file. #cmd("MKD") gets the same reply. A retry gets the same
      answer. Before FTPD 1.3.0
      the reply was #cmd("452"), which invites a client to try again.],
    [#cmd("552 Requested file action aborted: no space for the file. ...")],
      [The file system is full during the write, or the file has reached the
      largest size that UFS supports. The text ends with "The file on the
      server is incomplete."],
    [#cmd("452 No space left on device")],
      [UFSD reports a lack of space for a command other than #cmd("STOR"), for
      example #cmd("MKD"). A retry later can work.],
    [#cmd("451 Write failed on the server after n bytes received: the file is incomplete.")],
      [The write failed and UFSD gave no reason.],
    [#cmd("426 Connection closed; transfer aborted after n bytes. The file on the server is incomplete.")],
      [The data connection broke.],
    [#cmd("553 Path name too long")], [The name is longer than UFS allows.],
  )
]

A file in UFS can have about 4 MB at most. This is the size that UFS reaches
with 16 direct blocks and 1024 indirect blocks of 4096 bytes: 4.06 MB. A
larger file gets the 552 reply for the write even on an empty file system.

After a failed #cmd("STOR") the incomplete file stays in the directory. FTPD
cannot tell a file that the command has created from one that it has replaced,
and the replaced content is gone in both cases. Delete the file or send it
again.

Other errors of UFSD map to these replies: #cmd("550") for a missing file or
directory, a file that exists, a name that is not a directory or is one, a
directory that is not empty, a file system that is read-only, and a refused
permission. #cmd("451") is the reply for an I/O error and for any other code.
