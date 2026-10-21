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
      #cmd("SITE FILETYPE=JES") (@ug-other-jes).],
    [UFS], [work in the Unix-like file systems that UFSD serves. A client
      switches to it by changing to a directory that starts with a slash
      (@ug-other-ufs).],
  )
] <ug-intro-tab>

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

FTPD 1.3 is built against LIBC/370 2.24.1, and its load module is
link-edited authorized (AC(1)). Which libraries to install, how to
authorize them and how to upgrade from an earlier release is the subject of
the installation guide in the #cmd("docs/") directory of the repository
(#cmd("docs/installation.md")).

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
    [The values of #cmd("SITE JESJOBNAME"), #cmd("JESOWNER") and
      #cmd("JESSTATUS") are upper case whatever the client typed.],
      [@ug-site-case],
    [#cmd("APPE") appends to a sequential data set and creates what is
      missing. It refuses an existing member of a PDS. Before, it replaced
      the data set like #cmd("STOR").], [@ug-data-appe],
    [A UFS file system without free inodes answers #cmd("552") instead of
      #cmd("452").], [@ug-other-ufs],
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
  )
] <ug-intro-new-tab>
