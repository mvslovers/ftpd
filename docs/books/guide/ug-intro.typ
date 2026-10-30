#import "../bookmaster/bookmaster.typ": *

= Introducing FTPD <ug-intro>

#idx("FTPD")
FTPD (ftpd) is an FTP server for MVS 3.8j. It runs as a started task, takes
connections from any FTP client, logs the user in with the security system
of the host, and gives the client three views of the system: its MVS data
sets, the job entry subsystem (JES), and the UFS file systems of UFSD. This
chapter describes what FTPD does, how it names things, and what it needs.

== What FTPD Does <ug-intro-what>

#idx("FTPD", "tasks")
FTPD speaks the FTP protocol on one TCP port, by default 2121. A client
connects, logs in with an MVS user ID and password, and then moves data sets
to and from the host:

#tab(caption: [What a client can do with FTPD])[
  #table(columns: (1.5in, 1fr),
    [View], [What it offers],
    [MVS data sets], [list data sets and partitioned data set members,
      fetch them, store them, append to a sequential data set, delete and
      rename them, create and remove partitioned data sets.
      This is the view a session starts in (@ug-data).],
    [JES], [submit a job by storing it, list jobs, fetch the spool output of
      a job, cancel a job. A client switches to it with
      #cmd("site filetype=jes") (@ug-jes).],
    [UFS], [work in the Unix-like file systems that UFSD serves. A client
      switches to it by changing to a directory that starts with a slash
      (@ug-ufs).],
  )
] <ug-intro-tab>

== Finding a Task <ug-intro-tasks>

#idx("tasks")
The chapters for the user follow the work that a user does. Each has a session
that was typed at a real FTPD with a command line client:

#tab(caption: [Where to find a task])[
  #table(columns: (2.6in, 1fr),
    [I want to ...], [Chapter],
    [connect and log in, or find out why the login fails], [@ug-first],
    [download or upload a data set, in text or in binary], [@ug-data-retr and
      @ug-data-stor],
    [create a new data set with #cmd("site recfm"), #cmd("site lrecl") and
      #cmd("site blksize")], [@ug-data-create],
    [read, write, list and delete the members of a partitioned data set],
      [@ug-data-pds],
    [move several data sets with #cmd("mget") and #cmd("mput")], [@ug-data-mget],
    [rename or delete a data set], [@ug-data-manage],
    [submit a job, see its status, fetch its output, delete it], [@ug-jes],
    [work with UFS files], [@ug-ufs],
    [find out what an error reply means], [@ug-errors],
    [install FTPD, set up security, run the started task, remove FTPD],
      [@ug-install, @ug-security, @ug-ops, @ug-remove],
  )
]

The sessions are typed with #cmd("tnftp") (@ug-first-read). For each client
command, the protocol command that FTPD receives is in the _FTPD Reference_, in
"Client Commands and Protocol Commands".

#idx("session")
Each client connection is a _session_ with its own user, working directory
and allocation settings. FTPD serves up to ten sessions at a time by
default (the parameter #cmd("MAXSESSIONS"), described in “The FTPDPRM
Parameters” in the _FTPD Reference_).

== How FTPD Names Things <ug-intro-names>

#idx("names")
The words this book uses, in the way FTPD uses them:

- *A data set* is an MVS data set, named by qualifiers of up to eight
  characters separated by periods, 44 characters at most. A *partitioned
  data set* (PDS) has *members*, written #cmd("DSN(MEMBER)").
- *The working directory* of the MVS view is not a directory. It is a
  prefix that FTPD puts in front of a name the client gives without quotes
  (@ug-data-names).
- *The started task* is the FTPD address space. Its user ID is the identity
  RAKF assigns to it, and it is not the user ID of any client
  (@ug-security-stc).

In prose this book says FTPD. Where a command, a path or a name on the
host has it, the name is #cmd("ftpd"), as in the repository.

== What FTPD Needs <ug-intro-needs>

#idx("requirements")
FTPD runs on MVS 3.8j with these parts:

- *RAKF* (or another security product with the same interface) for the
  logon and the data set checks (@ug-security).
- *JES2*, if the JES view is used.
- *UFSD*, if the UFS view is used. UFSD is a soft requirement: without it
  every UFS request is answered with #cmd("550 UFS service not available").

FTPD 1.3 is built against LIBC/370 2.25.1, and its load module is
link-edited authorized (AC(1)). FTPD 1.3.1, 1.3.2 and 1.3.3 carry the client library
of UFSD 1.5.2 (@ug-install-ufsd). Which libraries to install, how to
authorize them and how to upgrade from an earlier release is the subject of
@ug-install.

== What Is New in FTPD 1.3.3 <ug-intro-new-133>

#idx("FTPD 1.3.3", "changes")
FTPD 1.3.3 is a patch release. It changes these behaviours against 1.3.2 and
adds no function. Each is described where the book explains the subject:

#tab(caption: [Changes in FTPD 1.3.3])[
  #table(columns: (2.3in, 1fr),
    [Change], [Where],
    [A job that you submit in the JES view gets #cmd("USER") and
      #cmd("PASSWORD") on its JOB statement, and FTPD finds that statement only
      as #cmd("//name JOB"): two slashes, a name of one to eight characters,
      blanks and the operation. Before, any card with the word JOB between
      blanks counted, a comment card too. The cards went behind it, the real
      JOB statement had none, and the job ran without an owner.],
      [@ug-jes-submit],
    [FTPD puts #cmd("USER") and #cmd("PASSWORD") behind the real end of the JOB
      statement: the operand field of the last card ends at the first blank
      outside a quoted string, and the comma goes directly behind it. A JOB
      statement that cannot take them is refused with
      #cmd("550 JOB statement cannot be continued with the credentials; job not submitted"),
      and no card of it reaches JES2. These are a card whose operands run to
      column 71, a card that ends in a comma without a continuation card, a
      statement that ends the stream or has more than 16 cards, a card that
      ends inside a quoted string, and an operand field that ends with a
      parenthesis still open. Before, FTPD put the comma at the end of the
      card, comment included. A comma in a comment continues nothing, and there
      was no room for it in column 71. The cards with the credentials were
      statements of their own, and the job ran without an owner.],
      [@ug-jes-credentials, @ug-errors, _FTPD Reference_,
      "The 550 Reply for a JOB Statement"],
    [A #cmd("get") from the UFS view that cannot read the file to the end is
      answered with
      #cmd("451 Read error on the server after N bytes: transfer is incomplete."),
      and one whose data connection breaks with
      #cmd("426 Connection closed; transfer aborted after N bytes."). Before,
      both got #cmd("226 Transfer complete"), and a client kept a part of the
      file as if it were the whole.],
      [@ug-ufs-read-errors, @ug-errors, _FTPD Reference_, "The 451 Reply"],
    [The list of the spool files of a job (#cmd("dir JOB00036")) has the layout
      of z/OS: the job line, a table with an ID for each file, and the last
      line #cmd("N spool files"). The ID is the number of
      #cmd("get JOB00036.n"). The input files of the job are not listed.
      Before, the list had a layout of its own, and a client that reads the
      layout of z/OS, such as the job commands of the Zowe CLI plug-in for
      z/OS FTP, found no table in it.],
      [@ug-jes-output, _FTPD Reference_, "The JES View"],
    [#cmd("SITE JESOWNER") with another user ID or #cmd("*") needs the right to
      see the jobs of others. A user without it gets
      #cmd("200-User X is not authorized to filter on JESOWNER, JESOWNER remains X")
      and #cmd("200 SITE command was accepted"), and sees the own jobs.
      Before, FTPD answered #cmd("501 JESOWNER is not permitted"), and a client
      that sends #cmd("JESOWNER=*") with every job request, such as the Zowe CLI
      plug-in for z/OS FTP, stopped on it. A value that is no owner at all
      is still answered with #cmd("501").],
      [@ug-jes-list, _FTPD Reference_, "JESOWNER"],
    [When two sessions ask for a passive port at the same moment, FTPD takes
      the next free port of #cmd("PASVPORTS") for the second one. Before,
      #cmd("PASV") and #cmd("EPSV") of that session were answered
      #cmd("425 Cannot open passive connection") although the range had free
      ports.],
      [_FTPD Reference_, "The 425 Reply"],
    [FTPD asks the host for room for 128 waiting connections, where it asked
      for 10. Before, a burst of more clients than that lost some of them: the
      connect of the client worked, and the #cmd("220") came late or not at
      all.],
      [@ug-ops-ses, _FTPD Reference_, "The Threads"],
    [An authorized FTPD checks the security system once at the start. If the
      system accepts a login that cannot be valid, FTPD writes
      #cmd("FTPD004E") and answers every #cmd("PASS") with
      #cmd("530 Login not possible: the security check of this server is not working ..."). After the tables are corrected, FTPD has to be restarted.
      A normal start leaves one #cmd("RAKF0004") line for the check user
      #cmd("?FTPDPRB") on the console. FTPD also refuses a login whose
      security environment belongs to another user, with
      #cmd("530 Login incorrect."). Before, there was no check.],
      [@ug-security-check, @ug-ops-check, _FTPD Reference_, "The Check of the
      Security System"],
  )
] <ug-intro-new-133-tab>

== What Is New in FTPD 1.3.2 <ug-intro-new-132>

#idx("FTPD 1.3.2", "changes")
FTPD 1.3.2 is a patch release. It changes these behaviours against 1.3.1 and
adds no function. Each is described where the book explains the subject:

#tab(caption: [Changes in FTPD 1.3.2])[
  #table(columns: (2.3in, 1fr),
    [Change], [Where],
    [#cmd("LIST") and #cmd("NLST") with a name in quotes, or with a pattern such
      as #cmd("'SYS2.**'"), read the catalog at the level of that name, whatever
      the working directory is. Before, FTPD read only the working directory,
      and the list was empty unless the name was below it. Clients such as the
      Zowe FTP plugin ask this way.],
      [@ug-data-list-abs, _FTPD Reference_, "LIST and NLST"],
    [A data set listing that has nothing to show is answered with
      #cmd("550 No data sets found."), before the data connection opens. Before,
      FTPD sent #cmd("150"), an empty list and #cmd("226") when the level held
      data sets or was a data set itself.],
      [@ug-data-list-none, @ug-errors],
    [A #cmd("**") that is not a qualifier of its own, as in #cmd("**ASM") or
      #cmd("SYS2.A**"), is refused with
      #cmd("501 ** must be preceded and followed by a period or blank").
      Before, FTPD took it for a wider #cmd("*"). The match of a pattern against
      one name is limited to 10000 steps.],
      [@ug-data-list, _FTPD Reference_, "The 501 Reply"],
    [A parenthesis in a name is checked to the end of the name, in #cmd("CWD"),
      in the arguments of #cmd("LIST") and #cmd("NLST") and in the member of
      #cmd("DELE"), #cmd("RNFR"), #cmd("RNTO") and the transfers.
      #cmd("CWD"), #cmd("MKD") and #cmd("RMD") act on a whole data set and
      refuse a name with a member with #cmd("501"). Before, on some systems,
      #cmd("rmdir 'DSN(MEMBER)'") deleted the member\; on others it was
      answered with #cmd("550") and deleted nothing.],
      [@ug-data-parens, _FTPD Reference_, "Member Names"],
  )
] <ug-intro-new-132-tab>

== What Is New in FTPD 1.3.1 <ug-intro-new-131>

#idx("FTPD 1.3.1", "changes")
FTPD 1.3.1 changes these behaviours against 1.3.0. Each is described where
the book explains the subject:

#tab(caption: [Changes in FTPD 1.3.1])[
  #table(columns: (2.3in, 1fr),
    [Change], [Where],
    [#cmd("LIST") and #cmd("NLST") with #cmd("'DSN(pattern)'") answer with the
      members of that partitioned data set that fit the pattern, from any
      working directory. A data set that is not there, is not partitioned or
      has no such member gets #cmd("550 No data sets found."). Before, the
      list was empty with the reply #cmd("226"). Clients such as the Zowe FTP
      plugin ask for the members this way.],
      [@ug-data-list, _FTPD Reference_, "LIST and NLST"],
    [#cmd("SITE ISPFSTATS") is accepted and ignored, with any value or none,
      and answers #cmd("200 SITE command was accepted"). Before, the reply was
      #cmd("Unrecognized parameter"), which the same clients take as a
      failure.], [_FTPD Reference_, "The SITE Command"],
    [FTPD carries the client library of UFSD 1.5.2. Under parallel uploads, a
      block that the server refuses because its buffer pool is empty is sent
      again; before, the upload could fail. A write that the server rejects is no
      longer counted as written. A #cmd("put") whose request never reached
      the server gets #cmd("451 Write failed on the server ..."), not a
      success. On the read side, the library could report end of file early
      when that pool was empty, and the file position could move ahead of the
      data delivered; a #cmd("get") from the UFS view could end short. The
      library no longer does this. Install the UFSD 1.5.2 server too: UFSD
      1.5.1 and earlier could give one disk block to two files while writing,
      without an error, and a written file then held foreign data. The fix is
      in the server, and it does not repair files that were damaged before.],
      [@ug-ufs-errors, @ug-install-ufsd],
    [Behind NAT, port forwarding or a container network, set #cmd("PASVADR")
      to the address that the client can reach. With the default #cmd("ANY")
      some clients wait 30 seconds before every transfer. New in the
      troubleshooting table and in the notes on the configuration.],
      [@ug-install-trouble, @ug-ops-config],
    [Create the profiles of RAKF with #cmd("RDEFINE"), #cmd("PERMIT") and
      #cmd("ADDSD"), and reload with #cmd("S RAKFPROF"). Do not edit
      #cmd("PROFILES") by hand. The set-up of #cmd("FTPAUTH") uses the
      commands, and the administrator must be connected to the group
      #cmd("RAKFADM").], [@ug-install-rakfsetup, @ug-security-jesspool],
    [A complete session with JESSPOOL, from the profiles to the jobs that
      each user sees, with the log lines of the refusals. A refused check
      writes nothing on the operator console.],
      [@ug-jes-sample, @ug-ops-log],
  )
] <ug-intro-new-131-tab>

== What Is New in FTPD 1.3.0 <ug-intro-new>

#idx("FTPD 1.3", "changes")
FTPD 1.3.0 changes these behaviours against 1.2.0. Each is described where
the book explains the subject:

#tab(caption: [Changes in FTPD 1.3.0])[
  #table(columns: (2.3in, 1fr),
    [Change], [Where],
    [The started task no longer logs on as #cmd("FTPD") at startup. It keeps
      the identity RAKF gives it, and says which in #cmd("FTPD004I") or
      #cmd("FTPD004W").], [@ug-security-stc],
    [A user sees, reads and cancels only the own jobs in the JES view. The jobs
      of other users need a profile in the RAKF class #cmd("JESSPOOL"). Before,
      every user could list, read and cancel the jobs of everybody. Define the
      profiles before you upgrade. The class exists from RAKF 2.0.0; FTPD 1.3.0
      itself runs on an older RAKF too, and there nobody sees the jobs of
      others.],
      [@ug-jes-others, @ug-security-jesspool],
    [#cmd("SITE JESOWNER") with another user ID or #cmd("*") needs the same
      right. Without it the reply is #cmd("200-User X is not authorized to
      filter on JESOWNER, JESOWNER remains X") and #cmd("200 SITE command was
      accepted"), and the user sees the own jobs. FTPD 1.3.0 to 1.3.2 answered
      #cmd("501 JESOWNER is not permitted") here. Before 1.3, the value was
      accepted and the owner was compared as a prefix. Now the whole owner is
      compared, and a value that is longer than eight characters is refused
      with #cmd("501 JESOWNER is not permitted"), not cut.], [@ug-jes-list],
    [The values of #cmd("SITE JESJOBNAME"), #cmd("JESOWNER") and
      #cmd("JESSTATUS") are upper case whatever the client typed.],
      [@ug-site-case],
    [#cmd("APPE") appends to a sequential data set and creates what is
      missing. It refuses an existing member of a PDS. Before, it replaced
      the data set like #cmd("STOR").], [@ug-data-manage],
    [A UFS file system without free inodes answers #cmd("552") instead of
      #cmd("452").], [@ug-ufs],
    [A connection that the worker pool refuses is answered with
      #cmd("421") and closed. Before, the control connection stayed open and
      the session was never freed.], [@ug-ops-ses],
    [A listing that runs out of storage answers #cmd("451 Insufficient storage to list ...") instead of an empty list.], [@ug-data-list],
    [#cmd("STOR") and #cmd("APPE") of a data set under retention answer
      #cmd("550") with the expiration date. Before, the session waited for an
      operator reply to #cmd("IEC507D").], [@ug-data-retained],
    [A member #cmd("STOR") or #cmd("APPE") into a partitioned data set with a
      full directory answers #cmd("552") with the remedy. Before, the answer
      was #cmd("451"), and a retry failed the same way.], [@ug-data-dirfull],
    [#cmd("STATS") counts each transferred byte once, and only for sessions
      that have ended. Before, a transfer of a data set counted two to three
      times.], [@ug-ops-con],
    [#cmd("SITE") accepts the shortest forms of its keywords, such as
      #cmd("PRI=5") for #cmd("PRIMARY=5"). Before, a short form was an unknown
      word.], [@ug-site-form],
    [#cmd("SITE RETPD=days") sets the retention period of the data sets that
      #cmd("MKD") and #cmd("STOR") create. The parameter #cmd("MAXRETPD") limits
      it, and a larger value is answered with #cmd("501"). Before, there was
      no #cmd("RETPD").], [@ug-site-keys],
    [The parameter #cmd("DEFDIRECTORY") sets the directory blocks of a
      partitioned data set that #cmd("MKD") creates. Before, it was always 10
      unless the client sent #cmd("SITE DIRECTORY=").], [@ug-site-keys],
    [A #cmd("MAXSESSIONS") of 1, 2 or 3 works. FTPD runs one worker thread for
      each allowed session. Before, no session started.], [@ug-ops-ses],
    [A text line that is longer than the record of the data set makes
      #cmd("STOR") and #cmd("APPE") fail with #cmd("451"), and a data set that
      the command created is removed. Before, FTPD cut the line without a
      word and answered #cmd("250"). #cmd("SITE TRUNCATE") brings the cutting
      back, and the #cmd("250") then says so. #emph[This is a change of
      behaviour: a script that relied on the silent cut now fails.]],
      [@ug-data-long],
    [#cmd("SITE JESSTATUS") takes #cmd("ALL"), #cmd("INPUT"), #cmd("ACTIVE") or
      #cmd("OUTPUT") and filters the job list, #cmd("RETR") and #cmd("DELE") of
      the JES view. Before, FTPD stored any value and listed every job.],
      [@ug-jes],
    [#cmd("SITE RDW") and a #cmd("SITE SBSENDEOL") other than #cmd("CRLF") are
      answered with #cmd("501"). Before, the answer was #cmd("200") and
      nothing changed.], [@ug-site-keys],
    [#cmd("AUTHUSER"), #cmd("JESINTERFACELEVEL") and the lines of the form
      #cmd("VOLSER,UNIT") in #cmd("FTPDPRM") are reported as ignored, in the
      log and with one console message #cmd("FTPD062W"). Before, FTPD read
      them without a word. #cmd("CONFIG") no longer writes #cmd("FTPD047I") and
      #cmd("FTPD048I").], [@ug-ops-config],
    [The listener takes every connection that waits on its next look, which
      it takes once each second. A burst of clients is greeted within about
      one second. Before, it took one connection per look, and ten clients
      that connected together were greeted over about ten seconds.],
      [@ug-ops-ses],
    [#cmd("STOR") or #cmd("APPE") of a data set under the expiration date
      #cmd("99000") answers #cmd("550") with the expiration date, like a retained data
      set. Before, the session waited for an operator reply to
      #cmd("IEC507D").], [@ug-data-retained],
    [After a #cmd("STOR") failed on a full directory of a partitioned data set,
      the data set can be renamed and its old name created again with #cmd("MKD"),
      and #cmd("P FTPD") ends without the abend SA0A. Before, the old name
      stayed allocated to the started task, #cmd("MKD") of it was answered
      with #cmd("550"), and the stop ended with the abend SA0A.],
      [@ug-data-dirfull],
    [The procedure of the started task no longer needs the #cmd("HASPCKPT") and
      #cmd("HASPACE1") DD statements. FTPD finds the JES2 checkpoint and the
      spool itself. Keep a statement only when it is needed (another
      #cmd("DSNPRFX"), or a checkpoint that is neither cataloged nor on an
      online volume). Before, the JES view needed both.],
      [@ug-install-jes],
    [#cmd("get") and #cmd("delete") in the JES view check the job ID before
      they ask JES2. An ID that is not #cmd("JOB"), #cmd("STC") or #cmd("TSU")
      with five digits, such as #cmd("JOB*"), is answered with
      #cmd("501 Invalid job ID"). Before, it went to JES2 and was answered with
      #cmd("550 Jobid ... not found ...").], [@ug-jes-badid],
    [#cmd("SITE DCBDSN=dsn") takes the record format, record length and block
      size of a cataloged data set for the data sets that follow. Before,
      #cmd("DCBDSN") was an unknown word.], [@ug-site-keys],
    [A FTPD that is not authorized (#cmd("FTPD003W")) answers every
      #cmd("PASS") with #cmd("530 Login not possible: FTPD is not APF authorized ..."),
      and the session stays open. Before, #cmd("PASS") was not answered, and
      the session was closed after the third one. The new message
      #cmd("FTPD077E") tells the operator that the reset of the identity
      ended in an ABEND during a recovery.],
      [@ug-install-auth, @ug-ops-abend],
    [In #cmd("TYPE A") a #cmd("RETR") of a data set with variable or undefined
      record format sends one line for each record or block. Before, FTPD read
      the data set as a stream of bytes across the record boundaries, joined
      the records and cut the result again every #cmd("LRECL") bytes. Load
      modules (record format #cmd("U")) belong in #cmd("TYPE I").],
      [@ug-data-retr],
    [#cmd("FTPD076W") says whether the data set stays allocated to the started
      task after a close that ended in an ABEND: #cmd("DD STILL HELD") or
      #cmd("DD RELEASED"). Before, it said #cmd("DD STILL HELD") in both
      cases.], [@ug-ops-abend],
    [The log lines with #cmd("reason=ERR") of #cmd("dir JOBnnnnn"),
      #cmd("RETR") and #cmd("DELE") in the JES view are limited to 16 for a
      session. FTPD writes #cmd("JES error: user=USERID and n more ERR") when
      the session ends. Before, there was no limit.], [@ug-ops-log],
  )
] <ug-intro-new-tab>
