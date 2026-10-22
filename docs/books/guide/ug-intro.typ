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
link-edited authorized (AC(1)). Which libraries to install, how to
authorize them and how to upgrade from an earlier release is the subject of
@ug-install.

== What Is New in FTPD 1.3 <ug-intro-new>

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
      profiles before you upgrade.],
      [@ug-jes-others, @ug-security-jesspool],
    [#cmd("SITE JESOWNER") with another user ID or #cmd("*") needs the same
      right, and answers #cmd("501 JESOWNER is not permitted") without it.
      Before, it answered #cmd("200") for any value, and the owner was compared
      as a prefix. Now the whole owner is compared, and a value that is longer
      than eight characters is refused, not cut.], [@ug-jes-list],
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
  )
] <ug-intro-new-tab>
