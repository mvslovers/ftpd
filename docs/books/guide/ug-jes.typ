#import "../bookmaster/bookmaster.typ": *

= Submitting Jobs and Reading Their Output <ug-jes>

#idx("JES")#idx("view")#idx("FILETYPE")
Besides MVS data sets, FTPD gives a client a view of the jobs in JES2. In this
view a client submits a job, lists the jobs, reads the output of a job, and
cancels a job. The view uses the same client commands as the data set view
(@ug-data), and the commands then do something else. A session starts in the
data set view. This chapter goes through the tasks of the JES view, one session
for each. The sessions run on one system in sequence, so the job IDs follow one
another. The last section is a separate run of the whole story, from the
profiles to the refusals, with its own job IDs (@ug-jes-sample).

A user works on the own jobs. The jobs of other users stay hidden, unless the
administrator allows them with a profile in the RAKF class #cmd("JESSPOOL")
(@ug-jes-others). That takes RAKF 2.0.0 or later, the release that has the
class; FTPD 1.3 does not need it to run. Before FTPD 1.3.0 every user could
list, read and cancel the jobs of everybody.

The view reads the JES2 checkpoint and the spool, which FTPD finds itself; the
procedure of the started task needs no statements for them
(@ug-install-jes). When FTPD cannot open the checkpoint, #cmd("dir") and #cmd("ls") end
with #cmd("451 Unable to open JES checkpoint"), and #cmd("get") and
#cmd("delete") answer #cmd("550 Jobid ... not found ..."). #cmd("put") does not
read the checkpoint.

== Switching to the JES View and Back <ug-jes-switch>

A client switches to the view with a #cmd("SITE") command (@ug-site), which
the client command #cmd("site") sends, and back with another one:

```
ftp> site filetype=jes
ftp> site filetype=seq
```

#cmd("quote site filetype=jes") does the same, for a client without
#cmd("site"). The commands #cmd("STOR"), #cmd("LIST"), #cmd("NLST"),
#cmd("RETR") and #cmd("DELE") go to JES2 as long as the view is on. In client
commands: #cmd("put") submits a job, #cmd("dir") and #cmd("ls") list jobs,
#cmd("get") fetches output, and #cmd("delete") cancels a job. All other
commands work as before, and #cmd("pwd") shows the working directory of the
view that was active when the client switched (the data set prefix in the last
session of this chapter, or the UFS path (@ug-ufs)). #cmd("site filetype=seq")
returns to the view that was active when the client switched to JES, also
after a #cmd("cd") in the JES view. A session that never switched to JES has
the data set view as that view, so #cmd("site filetype=seq") in UFS moves it to
the data set view.

== Submitting a Job <ug-jes-submit>

#idx("job", "submit")#idx("internal reader")
The session switches to the JES view, submits a job with #cmd("put"), and
lists the jobs. The job is the file #cmd("hello.jcl") of the local directory:

#screen(```
//FTPHELLO JOB (ACCT),'FTP TEST',CLASS=A,MSGCLASS=H
//STEP1    EXEC PGM=IEBGENER
//SYSPRINT DD SYSOUT=H
//SYSUT1   DD *
HELLO FROM A JOB THAT FTPD SUBMITTED
/*
//SYSUT2   DD SYSOUT=H
//SYSIN    DD DUMMY
```)

#screen(raw(read("../ex/ug-jes/j-submit.txt"), block: true))

#cmd("put") sends the JCL to the JES2 internal reader (#cmd("STOR")). FTPD
ignores the file name of the command, and the client prints the name of the
local file for both sides. The reply to the command shows the transfer: the
start is #cmd("125 Sending Job to JES internal reader FIXrecfm 80"), and the end
is a reply of two lines, #cmd("250-It is known to JES as JOB00036") and
#cmd("250 Transfer completed successfully."). The job ID is the one that the
following commands use. The job in the example ends with the return code 0000
by the time #cmd("dir") lists it.

The job's output stayed in the class #cmd("H") because the JCL says
#cmd("MSGCLASS=H") and #cmd("SYSOUT=H"). A class that the system purges at once
leaves nothing to fetch (@ug-jes-output). Use a held class for the output you
want to read.

FTPD reads the JCL line by line. A line is one card of 80 columns. A line that
is longer is cut at column 80, a shorter one is filled with blanks, and a
carriage return at the end of a line is removed. FTPD converts the text to
EBCDIC only in text mode (#cmd("ascii")). In binary mode it sends the bytes as
they come and does not find the JOB statement, so it adds nothing to it. Send
JCL as text.

FTPD looks for the JOB statement, the first card that has the word JOB between
blanks. It changes the statement in this way:

- At the end of the JOB statement, FTPD adds a comma and then up to
  three continuation cards.
- The first card is #cmd("NOTIFY=DUMMY,"). FTPD adds it only if the JOB
  statement has no #cmd("NOTIFY") yet.
- The second card is #cmd("USER=") with the user ID of the session.
- The third card is #cmd("PASSWORD=") with the password of the session.

The job runs under the identity of the user, not under the one of the started
task (@ug-security-jes). FTPD does not look for a #cmd("USER=") or a
#cmd("PASSWORD=") that the JCL has already, so leave them out. FTPD adds the
comma only if the last line of the JOB statement ends before column 71. Keep
that line shorter.

#note[*A defect* (ftpd\#277): FTPD takes the first card of the stream that has
the word JOB between blanks for the JOB statement, also a comment card. A
comment such as #cmd("//* RUN THE JOB NOW") before the real JOB statement gets
the three operands, and the real JOB statement gets none. The job then does not
run under the identity of the user. Until
a later release fixes this, put no card before the JOB statement that has the
word JOB between blanks, and write no comment with it.]

FTPD does not check the JCL. A JCL error shows in the list of the job
(@ug-jes-list), as the text #cmd("JCL ERROR") for the job.

If FTPD cannot hand the JCL over, the replies are
#cmd("451 Unable to open JES internal reader"),
#cmd("451 Failed to submit JCL to internal reader") and
#cmd("451 Failed to close internal reader"). A data connection that cannot be
opened gives #cmd("425 Cannot open data connection").

== Listing Jobs <ug-jes-list>

#idx("job", "list")
#cmd("dir"), #cmd("ls") (#cmd("LIST")) and #cmd("nlist") (#cmd("NLST")) give the
same list of jobs. In the session above, the reply that opens the list names
the filters of the session (below):

#screen(```
125 List started OK for JESJOBNAME=*, JESSTATUS=ALL and JESOWNER=IBMUSER
```)

The list has one line for each job after the header:

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
filters from the session. The next session submits a job with a JCL error, and
sets the three filters one after the other:

#screen(raw(read("../ex/ug-jes/j-filter.txt"), block: true))

The new job #cmd("FTPBAD") is in the list with #cmd("JCL ERROR") (the JCL of
the file #cmd("bad.jcl") has a misspelled keyword on its #cmd("EXEC")
statement). The filters are:

- *The owner:* #cmd("site jesowner=name") sets it. Without it the owner is the
  user ID of the session, so a user sees the own jobs. FTPD compares the whole
  owner of each job with the value: #cmd("ABC") does not match a job of
  #cmd("ABCD"). #cmd("site jesowner=*") lists the jobs of all users. The
  filter only narrows the view. A user ID other than the own one and #cmd("*")
  need the right to read those jobs (@ug-jes-others). Without it the reply is
  #cmd("501 JESOWNER is not permitted") and the filter stays as it was, as
  the two attempts of the session show. The reply does not say why and does
  not repeat the value. A pattern such as #cmd("ABC*"), a value of more than
  eight characters and a character other than a letter, a digit, #cmd("$"),
  #cmd("#") or #cmd("@") are refused in the same way, and FTPD never cuts a
  value to eight characters. The own user ID is always accepted. The header
  #cmd("125") names the filter that applies.
- *The job name:* #cmd("site jesjobname=name") limits the list to the jobs
  with this name. The session used #cmd("FTPH*"), and the list had only the job
  of the first session.
- *The status:* #cmd("site jesstatus=status") limits the list to the jobs in
  one status: #cmd("INPUT"), #cmd("ACTIVE") or #cmd("OUTPUT"). The default is
  #cmd("ALL"), every status. The session asked for #cmd("ACTIVE"), and no job
  was active. A job in one of the other states of the column STATUS
  (#cmd("XMIT"), #cmd("SETUP"), #cmd("RECEIVE"), #cmd("UNKNOWN")) is listed with
  #cmd("ALL") only. A status that is not one of the four is refused with a
  reply that says so in its first line, as the last #cmd("site") of the session
  shows, and the filter that was set stays.

The values are not case sensitive. A client sets the job name and the status
back to everything with #cmd("site jesjobname=*") and
#cmd("site jesstatus=all"), as the session does. #cmd("site jesowner=") with no
value, or with the own user ID, sets the owner back to the user of the session.
#cmd("site jesowner=*") does not set it back: it asks for the jobs of all users.

The list has at most 200 jobs. A job that the user may not see (@ug-jes-others)
is left out of the list and does not count. When the list reaches 200 jobs, it
ends with these two lines, and jobs after the 200th are not shown. The lines
also appear for a list of exactly 200 jobs, when nothing was left out:

#screen(```
250-JESENTRYLIMIT of 200 reached.  Additional entries not displayed
250 List completed successfully.
```)

Narrow the filters to see the others.

#note[*A job ID as the argument.* An argument that starts with #cmd("JOB"), in
any case, is read as a job ID and gives the list of the spool files of that job
(the next section). It is not a filter. To list jobs with a name that starts
with #cmd("JOB"), use #cmd("site jesjobname"). A job that does not exist, one
that the user may not see (@ug-jes-others) get the reply of #cmd("get"):
#cmd("550 Jobid ... not found ..."). So does an ID with #cmd("*") or a pattern,
which #cmd("dir") answers with the same #cmd("550"), where #cmd("get") and
#cmd("delete") refuse it with #cmd("501 Invalid job ID") (the next section).]

== Reading the Output of a Job <ug-jes-output>

#idx("job", "output")#idx("spool")
The command #cmd("dir JOB00036") lists the spool files of the job. The list
has the same layout as the list of a job on z/OS: the job line, a line of
dashes, a table with one line for each spool file, and a last line with the
number of files. The command #cmd("get") (#cmd("RETR")) fetches the files. The
session lists the spool files, fetches all of them in one file, fetches the
third one alone, and asks for two that do not exist:

#screen(raw(read("../ex/ug-jes/j-output.txt"), block: true))

The lines of the list are:

- *The job line:* the header of the job list (#cmd("JOBNAME  JOBID    OWNER    STATUS CLASS"))
  and the line of the job, the same line as in the list of the jobs
  (@ug-jes-list).
- *The table:* the columns are the ID, the step (#cmd("STEPNAME")), the
  procedure step (#cmd("PROCSTEP")), the output class (#cmd("C")), the DD name
  and the byte count. The ID counts the files from 1. It is printed with
  three digits.
- *The last line:* #cmd("5 spool files") is the number of lines of the table.
  The line says #cmd("files") also for a job with one file.

The ID is the number of #cmd("get JOB00036.n"). The list shows the files that
#cmd("get") can fetch, and no others. The files that JES2 wrote itself
(#cmd("JESMSGLG"), #cmd("JESJCL") and #cmd("JESYSMSG")) have the step
#cmd("JES2"), because they belong to no step of the job. The input files of the
job have no line and no ID: the JCL that was read (#cmd("JESJCLIN")), the input
text (#cmd("JESINTXT")) and the data of #cmd("SYSIN") that is part of the job
(#cmd("SYSUT1") of the example). A job that has no such file (it is still in
the input queue, or its output is purged) gets the header and the job line
alone, with no dashes, no table and no count. The list shows at
most 999 files.

In the example #cmd("JOB00036.3") is the third line, #cmd("JESYSMSG"), and
#cmd("JOB00036.9") is past the five. The first of the two error lines at the
end of the session shows this reply, and the second one is for a job that JES2
does not know. The byte count of the list is the number of records times the
record length of the file, so it is not exact.

#note[*Changed in FTPD 1.3.3.* Up to FTPD 1.3.2 the list of the spool files had
its own header (#cmd("JOBNAME  JOBID    STEPNAME PROCSTEP C DDNAME   BYTE-COUNT")),
repeated the job name and the job ID on every line, listed the input files too,
had no ID column and no last line. The number of #cmd("get JOB00036.n") was not
shown. A script that read the old
columns must read the new layout: the table follows the line with the header
#cmd("ID  STEPNAME PROCSTEP C DDNAME   BYTE-COUNT") and ends before the line
#cmd("N spool files").]

*Zowe CLI.* The job commands of the Zowe CLI plug-in for z/OS FTP read this
layout: the plug-in 3.1.1 finds the table by the header line with #cmd("ID")
and the end by the line #cmd("N spool files"). The commands that take a job ID
(#cmd("list spool-files-by-jobid"), #cmd("view job-status-by-jobid"),
#cmd("view all-spool-by-jobid"), #cmd("view spool-file-by-id") and
#cmd("download all-spool-by-jobid")) first send
#cmd("SITE FILETYPE=JES SBSENDEOL=CRLF JESJOBNAME=* JESOWNER=*"). FTPD accepts
#cmd("JESOWNER=*") only for a user who has READ on the entity #cmd("..") in the
class #cmd("JESSPOOL") (@ug-jes-others, the Reference has the table of the
values of #cmd("JESOWNER")). For any other user the reply is
#cmd("501 JESOWNER is not permitted"), and the plug-in stops with that error.
The first run is a user with READ on #cmd("..") (IBMUSER, a member of
#cmd("ADMIN")), the second one a user without it (BOB). The options of the
connection (host, port, user and password) are left out:

#screen(raw(read("../ex/ug-jes/j-zowe.txt"), block: true))

FTPD converts the text to ASCII in text mode (#cmd("ascii")). A line longer than
253 characters is cut. A spool file that holds no data keeps its number, and
a #cmd("get") of all the files sends nothing for it. The end is
#cmd("250 Transfer completed successfully."). A job whose output went to a
class that the system purges has nothing to fetch.

The error replies are:

#tab(caption: [The error replies of RETR in the JES view])[
  #table(columns: (2.6in, 1fr),
    [Reply], [Cause],
    [#cmd("501 Missing job ID")], [No argument.],
    [#cmd("501 Invalid job ID")], [The job ID is not #cmd("JOB"), #cmd("STC")
      or #cmd("TSU") followed by exactly five digits, as in #cmd("JOB*"),
      #cmd("JOB1") or #cmd("ABC00001"). FTPD does not ask JES2.
      See @ug-jes-badid.],
    [#cmd("550 Jobid JOB00042 not found for JESJOBNAME=*, JESSTATUS=ALL, JESOWNER=USERID")],
      [JES2 does not know the job, the job is not in the status that
      #cmd("JESSTATUS") names, or the job belongs to another user and the user
      may not see it (@ug-jes-others). One text answers all three cases, and it
      shows the filters of the session.],
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

=== A Job ID That FTPD Refuses <ug-jes-badid>

#idx("job", "job ID")#idx("501")
#cmd("get") and #cmd("delete") take one job ID. FTPD checks its form before it
asks JES2: the job ID is #cmd("JOB"), #cmd("STC") or #cmd("TSU") followed by
exactly five digits (for #cmd("get") with an optional point and the number of
the spool file, as in #cmd("JOB00042.3")). The check comes after FTPD has made
the letters upper case, so #cmd("job00042") is a valid ID. Every other
argument is answered with #cmd("501 Invalid job ID"), and no job is looked up,
read or cancelled. A wildcard such as #cmd("JOB*") is one of them. The session
fetches and cancels with IDs of the wrong form, fetches a job of the right
form that does not exist, lists with a pattern, lists the jobs, and cancels
the job:

#screen(raw(read("../ex/ug-jes/j-badid.txt"), block: true))

- *#cmd("get JOB*"), #cmd("get JOB1"), #cmd("get ABC00001"):* the three forms
  of #cmd("501 Invalid job ID"): a pattern, too few digits, and a prefix that
  is none of the three. The line #cmd("229 Entering Extended Passive Mode")
  before each answers the #cmd("EPSV") that #cmd("tnftp") sends before #cmd("RETR")
  (_FTPD Reference_, "Client Commands and Protocol Commands"); the
  #cmd("501") answers the #cmd("RETR").
- *#cmd("get JOB99999"):* the form is right. JES2 does not know the job, and
  the reply is the #cmd("550") of the table above.
- *#cmd("delete JOB*") and #cmd("delete JOB00035.1"):* the same
  #cmd("501"). #cmd("delete") has no spool file number: the point makes the
  ID invalid. The job is still in the list afterwards.
- *#cmd("dir JOB*"):* not changed. A list with an argument that starts with
  #cmd("JOB") is a request for the spool files of one job, and a pattern gets
  the #cmd("550 Jobid ... not found ...") of a job that does not exist.

Before FTPD 1.3.0 the three #cmd("get") and both #cmd("delete") of the example
went to JES2 and ended in #cmd("550 Jobid ... not found ...").

#note[*The job ID and the status.* #cmd("get"), #cmd("delete") and
#cmd("dir JOBnnnnn") find the job by its exact ID. The filters #cmd("JESOWNER")
and #cmd("JESJOBNAME") do not apply: a user who may see the job of another user
(@ug-jes-others) needs no #cmd("site jesowner") to fetch it. The 550 text names
the filters, but they do not take part in the search. #cmd("JESSTATUS") does
take part: with #cmd("site jesstatus=active") the output of a job that has
ended cannot be fetched, and a job that is not active cannot be cancelled. Set
#cmd("JESSTATUS") back to #cmd("ALL") first.]

== Jobs of Other Users <ug-jes-others>

#idx("job", "other users")#idx("JESSPOOL")
A session always lists, reads and cancels its own jobs. For a job of another
user FTPD asks RAKF, in the class #cmd("JESSPOOL"), whether the user may read
it, and for a #cmd("delete") whether the user may update it. The administrator
answers with profiles (@ug-security-jesspool). Without a profile a user sees
nothing of the jobs of others. That is the state of FTPD 1.3 until the
administrator acts. A RAKF older than 2.0.0 has no class #cmd("JESSPOOL"), so
there the jobs of others stay hidden for everybody (@ug-security-jesspool). FTPD 1.2.0 let every user list, read and cancel the jobs of
everybody, so an administrator who relied on that defines the profiles before
the upgrade.

The first session is the one of BOB, a user without any right to the jobs of
others. BOB has the job #cmd("JOB00039"), and ALICE the job #cmd("JOB00038"):

#screen(raw(read("../ex/ug-jes/j-other-none.txt"), block: true))

- #cmd("dir") lists the job of BOB and no other. The header names
  #cmd("JESOWNER=BOB").
- #cmd("site jesowner=ALICE") and #cmd("site jesowner=*") are refused with
  #cmd("501 JESOWNER is not permitted"). The filter stays as it was.
- #cmd("dir JOB00038"), #cmd("get JOB00038") and #cmd("delete JOB00038") get
  #cmd("550 Jobid JOB00038 not found ..."), the reply for a job that does not
  exist. The reply does not tell BOB whether the job exists, and the owner
  that it names is the filter of BOB, not the owner of the job.

What the user may do with a job of another user depends on the access that
the profile gives:

#tab(caption: [Access to the jobs of another user])[
  #table(columns: (1.5in, 1fr, 1fr),
    [Access], [#cmd("dir"), #cmd("dir JOBnnnnn"), #cmd("get")], [#cmd("delete")],
    [none, or no profile], [The job is hidden: the 550 of the session above.],
      [The same 550.],
    [#cmd("READ")], [The job is listed and its output is read.],
      [#cmd("550 Access denied - cannot cancel JOBnnnnn")],
    [#cmd("UPDATE"), #cmd("CONTROL"), #cmd("ALTER")], [As for READ.],
      [#cmd("250 Cancel successful")],
  )
]

The next session is the one of AUDIT1, a member of the group AUDIT, which has
READ on the jobs of all users (@ug-security-jesspool). The session lists
the own jobs, widens the list to all users, narrows it to ALICE, reads the
output of her job (the client shows the first lines of the file), and tries to
cancel it:

#screen(raw(read("../ex/ug-jes/j-other-read.txt"), block: true))

- The first #cmd("dir") is empty: AUDIT1 has no job of its own.
- #cmd("site jesowner=*") and #cmd("site jesowner=ALICE") are accepted,
  because AUDIT1 may read the jobs of all users. The list of all users has the
  jobs of ALICE and BOB. It has no started task: the owner of a started task is
  #cmd("SYSTEM"), and the profiles of the example give #cmd("SYSTEM") no
  access.
- #cmd("dir JOB00038") and #cmd("get JOB00038") work. They need no
  #cmd("site jesowner"), because the job ID alone names the job.
- #cmd("delete JOB00038") is refused with
  #cmd("550 Access denied - cannot cancel JOB00038"). This is the reply of a
  user who sees the job but has no right to cancel it. It differs from "not
  found" on purpose.

The third session is the one of IBMUSER, a member of the group ADMIN, which
has UPDATE on the jobs of all users. IBMUSER cancels the job of BOB:

#screen(raw(read("../ex/ug-jes/j-other-update.txt"), block: true))

#note[*Your own jobs do not depend on a profile.* A profile in the class
#cmd("JESSPOOL") decides only what a user may do with the jobs of other users.
It cannot hide the own jobs from their owner. A user ID that must not use the
spool at all is kept out at the login (#cmd("FTPAUTH"), @ug-security-ftpauth).]

#note[*The profiles apply to FTP only.* They say nothing about who can read the
spool through TSO, SDSF or another interface.]

FTPD writes a line to the log for most refusals, so that the operator can see
who asked for what (@ug-ops-log).

== Cancelling a Job and Leaving the View <ug-jes-cancel>

#idx("job", "cancel")
#cmd("delete JOB00042") (#cmd("DELE")) cancels the job and removes its output
from the spool. The session cancels the two jobs of the earlier sessions, lists
the jobs after each, and returns to the data set view:

#screen(raw(read("../ex/ug-jes/j-delete.txt"), block: true))

The reply is #cmd("250 Cancel successful"). The second #cmd("delete") of the
same job gets the #cmd("550"): the job is gone. The other errors are:

- #cmd("501 Missing job ID") without an argument.
- #cmd("501 Invalid job ID") when the argument is not #cmd("JOB"), #cmd("STC")
  or #cmd("TSU") with five digits, also for an ID with #cmd("*") or a pattern
  and for one with a point (@ug-jes-badid).
- #cmd("550 Access denied - cannot cancel JOB00042") when JES2 refuses, and
  when the job belongs to another user and the profile gives only #cmd("READ")
  (@ug-jes-others).
- #cmd("550 Failed to cancel JOB00042 (rc=n)") for any other code of JES2.
- #cmd("550 Jobid ... not found ...") for a job that JES2 does not know and
  for a job of another user that the profile hides.

#cmd("site filetype=seq") ends the JES view, and #cmd("pwd") shows the working
directory of the view that was active before. Any other value of #cmd("FILETYPE") ends the view too.

== A Complete Sample Session with JESSPOOL <ug-jes-sample>

#idx("JESSPOOL")#idx("job", "other users")#idx("console")
The sections above show each task on its own. This section runs the whole
story once on a fresh system, so that you can compare a first test with real
output. It is a separate run: the job IDs start again at #cmd("JOB00038") and
do not continue those of the sections above. FTPD 1.3 runs with RAKF 2.0.0.

*The setup.* ALICE and BOB have the default group #cmd("FTPUSERS"), and CAROL
has the group #cmd("USER"). IBMUSER is a member of #cmd("ADMIN"). The
users are made with the RAKF command #cmd("ADDUSER") in a TSO batch job, and
the procedure #cmd("RAKFUSER") loads the table; the profiles are made in the
same way and loaded with #cmd("RAKFPROF") (@ug-security-jesspool has the job
step by step):

#screen(```
ADDUSER ALICE PASSWORD(ALICEPW1) DFLTGRP(FTPUSERS)
ADDUSER BOB PASSWORD(BOBPW123) DFLTGRP(FTPUSERS)
ADDUSER CAROL PASSWORD(CAROLPW1) DFLTGRP(USER)
PE FTPAUTH CL(FACILITY) ID(FTPUSERS) ACCESS(READ)
RDEF * CL(JESSPOOL) UACC(NONE)
PE * CL(JESSPOOL) ID(ADMIN) ACCESS(UPDATE)
RDEF SYSTEM.* CL(JESSPOOL) UACC(NONE)
```)

The passwords are for this test only. The group #cmd("FTPUSERS") may read
#cmd("FTPAUTH") after the first #cmd("PE"), and MVS/CE has given the same
right to #cmd("ADMIN") from the start. So ALICE, BOB and IBMUSER may log in by
FTP, and CAROL may not. The profile #cmd("*") hides the jobs of every user from every other user,
and #cmd("ADMIN") may update all of them. The last line closes the started
tasks (@ug-security-jesspool). There is no profile for ALICE or BOB. A user
needs none for the own jobs.

*Own jobs need no profile.* ALICE logs in, switches to the JES view, submits
the job #cmd("payroll.jcl") (the same kind of job as in @ug-jes-submit, with
the name #cmd("PAYROLL")), lists the jobs, and fetches the output with
#cmd("get JOB00038"). The last client command, #cmd("quote retr JOB00038"),
shows why #cmd("get") is the command to use:

#screen(raw(read("../ex/ug-jes/j-sample-alice.txt"), block: true))

- #cmd("put") and #cmd("dir") need nothing but the login and
  #cmd("site filetype=jes"). The list names #cmd("JESOWNER=ALICE") and has the
  one job.
- #cmd("get JOB00038 alice.txt") sends all output files of the job into the
  local file, which starts with the job log. The line
  #cmd("229 Entering Extended Passive Mode") is the answer to the #cmd("EPSV")
  that #cmd("tnftp") sends before it sends #cmd("RETR") (@ug-jes-badid).
- #cmd("quote retr JOB00038") sends the #cmd("RETR") without that preparation.
  There is no data connection, and the reply is the pair
  #cmd("125 Sending all spool files for requested Jobid") and
  #cmd("425 Cannot open data connection"): FTPD finds the job, announces the
  transfer with the #cmd("125"), and then fails to open the connection. The
  #cmd("125") is no sign that anything was sent. Use #cmd("get"), which
  prepares the data connection.

*Jobs of others stay hidden.* BOB submits his own job, which has the ID
#cmd("JOB00039"), and then asks for the job of ALICE in the three ways a user
tries first:

#screen(raw(read("../ex/ug-jes/j-sample-bob.txt"), block: true))

- #cmd("site jesowner=ALICE") and #cmd("site jesowner=*") are refused with
  #cmd("501 JESOWNER is not permitted"). BOB has no READ on the jobs of ALICE
  or of all users, so the filter stays at #cmd("BOB"): the next list is the
  one of his own jobs. This is the check of @ug-jes-list.
- #cmd("dir JOB00038"), #cmd("get JOB00038") and #cmd("delete JOB00038") all
  get #cmd("550 Jobid JOB00038 not found ..."), the reply for a job that
  does not exist. The job is there, and BOB is told no more than that he cannot
  see it. The owner in the text is the filter of BOB, not the owner of the job.

A user who sees only the own jobs, as BOB does, is not a malfunction. It is
the state of a user for whom no profile gives more.

*An administrator sees and cancels everything.* IBMUSER is in the group
#cmd("ADMIN"), which has UPDATE on all jobs. The first #cmd("dir") lists only
the own jobs, and there are none. After #cmd("site jesowner=*") the list has
the jobs of ALICE and BOB, and the job log of ALICE can be fetched. The
#cmd("delete") cancels the job of BOB:

#screen(raw(read("../ex/ug-jes/j-sample-admin.txt"), block: true))

The list of all users has no started task, because the profile
#cmd("SYSTEM.*") closes them, also for #cmd("ADMIN"). After the
#cmd("delete") only the job of ALICE is left.

*A user without access to FTP.* CAROL has a good password. The profile
#cmd("FTPAUTH") gives her no READ, and the login is refused:

#screen(raw(read("../ex/ug-jes/j-sample-noauth.txt"), block: true))

FTPD answers #cmd("530 Not authorized for FTP access"), the client says
#cmd("Login failed"), and every other command gets #cmd("530 Not logged in.")
(@ug-first-fail). The JES view is not reached at all, so the profiles of the
class #cmd("JESSPOOL") play no part.

*Which job IDs are accepted.* ALICE is back. The ID is #cmd("JOB"), #cmd("STC")
or #cmd("TSU") with exactly five digits, in any case (@ug-jes-badid). Her
session fetches with the lower case ID, with two IDs of the wrong form, lists
the spool files of the job, cancels it, and lists again:

#screen(raw(read("../ex/ug-jes/j-sample-ids.txt"), block: true))

- #cmd("get job00038") works: FTPD makes the letters upper case.
- #cmd("get JOB38") has too few digits, and #cmd("get JOB*") is a pattern.
  Both get #cmd("501 Invalid job ID"). FTPD refuses the form before it asks
  JES2, and #cmd("501") never means that the job is hidden: a hidden or
  missing job gets the #cmd("550") of BOB's session.
- #cmd("dir JOB00038") lists the five spool files of the job (@ug-jes-output),
  and the own
  #cmd("delete") answers #cmd("250 Cancel successful"). The last list is
  empty.

*What the operator sees.* None of the refusals above wrote a RAKF message to
the console. The console of the test system was read from the first job to the
end of the sessions. It had the lines of JES2 for the jobs and the lines of
the RAKF procedures that loaded the tables (#cmd("RAKFUIDS4  USER TABLE UPDATED"),
#cmd("RAKFPROF7  RAKF PROFILES UPDATED")), and no line from RAKF for the
refused #cmd("site jesowner"), the hidden jobs, or the login of CAROL. That is
by design, not an oversight, and it comes from two places:

- FTPD asks RAKF through the library routine #cmd("racf_auth()") of LIBC/370,
  which sets the request flag "LOG=NONE" (#cmd("RACHECK_FLAG1_LOG_NONE")) in
  every call, for the class #cmd("JESSPOOL") as well as for #cmd("FTPAUTH") and
  for data sets.
- RAKF honours that flag. In its check routine #cmd("ICHSFR00"), a request with
  the flag sets an internal "no log" bit, and the path for a refused check
  tests the bit and skips the message #cmd("RAKF0005 INVALID ATTEMPT TO ACCESS
  RESOURCE") and the following #cmd("RAKF000A") line. The decision is the same
  with or without the flag: a refused check is still refused.

One message does not depend on the flag. A check against a class that RAKF
does not know writes #cmd("RAKF0012 UNDEFINED CLASS") every time, and that
only happens on a RAKF without the class #cmd("JESSPOOL"); it was not tried
here. So the console is the wrong place to look for a refusal. FTPD writes
its refusals to its own log, #cmd("STDOUT") (@ug-ops-log). For
#cmd("dir JOBnnnnn"), #cmd("get") and #cmd("delete") it writes one
#cmd("JES op denied") line for each job it hid or refused, with the reason. A plain #cmd("dir") hides
jobs without a line of their own: the number of hidden jobs is in the
#cmd("JES LIST") line of that listing.
