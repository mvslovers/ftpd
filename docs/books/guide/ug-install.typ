#import "../bookmaster/bookmaster.typ": *

= Installing FTPD <ug-install>

#idx("installation")
This chapter installs FTPD 1.3.0 on an MVS 3.8j system (TK4-, TK5, MVS/CE or
a Hercules build of your own) from the release package. The installation is
done by SMP Release 4, the SMP that comes with MVS 3.8j, not SMP/E. SMP keeps
a record of what was installed, and so there is a defined way to remove it
again (@ug-remove).

Report a problem as an issue at https://github.com/mvslovers/ftpd/issues.
Add the two build stamps that FTPD writes at the start (#cmd("FTPD000I") and
#cmd("FTPD005I"), @ug-install-start), the console messages and, for an
installation problem, the job output. Questions go to the Discord server of
the project.

The names of the data sets carry no version. #cmd("FTPD.LINKLIB") is the same
name in every release. The #cmd("STEPLIB") of the procedure and an APF entry
are written once. The price is that two releases cannot be installed side by
side: an upgrade replaces what is there. There is no way back to the previous
release. The install job accepts the new level, and SMP refuses a
#cmd("RESTORE") of an accepted function (@ug-remove-why). To take FTPD out of
the system, follow @ug-remove.

== What Is in the Package <ug-install-files>

#idx("package", "contents")
The package #cmd("ftpd-1.3.0-dist.zip") (or #cmd("ftpd-1.3.0-dist.tar.gz")) has
five files. In their names, 1.3.0 is the release.

#tab(caption: [The files of the installation package])[
  #table(columns: (2.7in, 1fr),
    [File], [What it is],
    [#cmd("README.md")], [The installation guide as a text file.],
    [#cmd("ftpd-1.3.0-load.xmit")], [The load module, in a form that TSO
      RECEIVE reads.],
    [#cmd("ftpd-1.3.0-samplib.xmit")], [The sample library: the procedure of
      the started task and a configuration pattern.],
    [#cmd("ftpd-1.3.0-alloc.jcl")], [Allocates the data sets SMP installs
      into. Run once.],
    [#cmd("ftpd-1.3.0-inst.jcl")], [Receives both XMIT files and installs
      them. Can be repeated.],
  )
]

FTPD is a single load module, #cmd("FTPD"). The sample library holds two
members: #cmd("FTPD") is the procedure of the started task and
#cmd("FTPDPRM0") is the configuration pattern. When the installation is done,
they are here:

#deflist(width: 1.6in,
  [#cmd("FTPD.LINKLIB")], [The load module. The #cmd("STEPLIB") points here.],
  [#cmd("FTPD.SAMPLIB")], [The patterns that you copy in @ug-install-proc.],
  [#cmd("FTPD.AFTPDLOD")], [The distribution library of SMP. A later PTF can
    be restored to the level in it. It is not a way back to the previous
    release (@ug-remove-why).],
)

== What You Need <ug-install-prereq>

#idx("prerequisites")
- MVS 3.8j running on Hercules, with SMP 4 in working order: the
  procedures #cmd("SMPREC") and #cmd("SMPAPP") and the #cmd("SYS1.SMP*") data
  sets. They are there on TK4-, TK5 and MVS/CE as delivered.
- RAKF, for the logins (@ug-install-rakf). This one is not optional. Which
  release matters for one thing only: letting users see the jobs of others
  takes RAKF 2.0.0 or later.
- UFSD, only if you want the UFS view (@ug-install-ufsd).
- A user ID that may submit jobs, change a procedure library of the started
  tasks and change a PARMLIB.
- A way to send a file to the host in binary (@ug-install-upload).
- About 15 cylinders of disk space: 10 for the two libraries that the
  allocation job creates, the rest for the two targets of TSO RECEIVE. One of
  them, the staging library, is scratched again by the last step of the
  install job.

=== UFSD Is Optional <ug-install-ufsd>

#idx("UFSD")
FTPD reaches three views: MVS data sets, JES and the file systems of UFSD
(@ug-ufs). Only the third needs UFSD, and nothing about the installation
does. The SYSMOD declares no prerequisite, #cmd("libufs") is linked into the
load module, and FTPD asks UFSD for a session the first time a client uses a
UFS path. On a system without the UFSD started task those commands answer

#screen(```
550 UFS service not available
```)

and everything else works. Install FTPD now and add UFSD later if you want it.
UFSD has to be running, not just installed: it is a started task that FTPD
talks to across address spaces. FTPD 1.3.0 needs UFSD 1.4.0 or later.

=== RAKF Is Required for Logins <ug-install-rakf>

#idx("RAKF")
FTPD has no user database of its own. Every #cmd("USER") and #cmd("PASS") is
verified by RAKF, and every data set access is checked against the RAKF class
#cmd("DATASET") under the identity of the user who is logged in
(@ug-security). On a system without RAKF nobody can log in, and no
configuration option turns that off. @ug-install-rakfsetup says what to set
up.

FTPD 1.3.0 does not need RAKF 2.0.0 to run. On a RAKF without the class
#cmd("JESSPOOL") (RAKF 1.2.x has none) it installs and starts, and in the JES
view every user sees, reads and cancels only the own jobs. To let users see the
jobs of others you need RAKF 2.0.0 or later, because the profiles of the class
#cmd("JESSPOOL") are the way to allow it (@ug-security-jesspool).

=== Authority <ug-install-auth>

#idx("authorization")
#idx("APF")
FTPD is link-edited with AC(1) and needs to be authorized for the switch of
the security environment that makes the data set I/O of each session run under
the identity of the client. There are two ways to get that.

*Through RAKF.* FTPD asks for authority itself at the start, with the SVC 244
that RAKF supplies. You need RAKF anyway, so FTPD needs no other setup. The
start shows #cmd("FTPD008I AUTHORIZED BY SVC (MODULE KEY 8)").

*Through the APF list.* Add #cmd("FTPD.LINKLIB") to
#cmd("SYS1.PARMLIB(IEAAPF00)"). MVS 3.8j reads the APF list at the IPL only,
so the entry costs one IPL. The name of the library does not change in later
releases, so it costs only that one. The start shows
#cmd("FTPD008I AUTHORIZED BY LIBRARY (MODULE KEY 0)").

An APF entry on MVS 3.8j names the data set and the volume serial. An entry
that is already there does not only permit a volume, it dictates one. The
allocation job says #cmd("UNIT=SYSDA") and no volume, so it puts the library
wherever #cmd("SYSDA") picks. If that is not the volume in the entry, you get
a library that exists, holds the right module, and is not authorized. Pin the
volume on the #cmd("LINKLIB") statement of the allocation job:

#screen(```
//LINKLIB  DD  DSN=FTPD.LINKLIB,DISP=(NEW,CATLG,DELETE),
//            UNIT=SYSDA,VOL=SER=your-apf-volume,
```)

Nothing reports the mismatch, because FTPD recovers from it: it falls back to
the SVC and comes up authorized. The only place where the two routes can be
told apart is #cmd("FTPD008I"). So #cmd("AUTHORIZED BY SVC") on a system where
you added an APF entry is not a decision of the site but a symptom.

FTPD warns and keeps running when the authorization fails
(#cmd("FTPD003W APF SETUP FAILED RC=n")). The server starts and accepts
connections, but nobody can log in. Every #cmd("PASS") gets the reply
#cmd("530 Login not possible: FTPD is not APF authorized. Ask the administrator to check the FTPD003W console message.").
The session stays open. The client can send #cmd("USER"), #cmd("SYST"),
#cmd("NOOP") and #cmd("QUIT"), and a command that needs a login gets
#cmd("530 Not logged in."). The refused #cmd("PASS") does not count as a failed
login, so the session is not closed after three of them. The log in the DD
name #cmd("STDOUT") has one line with the level #cmd("[ERROR]") for it per run
of the server. Before FTPD
1.3.0 FTPD did not answer #cmd("PASS") at all, and it closed the session after
the third one with #cmd("FTPD071E") and no reply. Treat #cmd("FTPD003W") as an
installation that is not finished.

The two routes differ in one respect that matters when you debug FTPD. An APF
entry authorizes the job step before the program is fetched, so MVS loads FTPD
into key 0 storage. The SVC sets the authority after the fetch, and the module
stays in key 8. FTPD runs in problem state with key 8 either way, so under an
APF entry it must never store into its own module storage. It does not, since
1.0.1; FTPD 1.0.0 abends with S0C4 at the start from an APF-authorized library.

== Sending the Two XMIT Files <ug-install-upload>

#idx("upload")
Both XMIT files are EBCDIC NETDATA streams. Send them in binary: no
translation between ASCII and EBCDIC, no conversion of line ends. They go
into sequential data sets with #cmd("RECFM=FB"), #cmd("LRECL=80") and
#cmd("BLKSIZE=3120").

You choose the names of the data sets. The install job names them in its
own #cmd("RECEIVE") commands (@ug-install-job). This book uses
#cmd("IBMUSER.FTPD.LOAD.XMIT") and #cmd("IBMUSER.FTPD.SAMP.XMIT").

Some ways to send need a data set that already exists. Where this book says
_pre-allocate_, allocate both as #cmd("DSORG=PS"), #cmd("RECFM=FB"),
#cmd("LRECL=80") and #cmd("BLKSIZE=3120"), with a primary of about 50
tracks and a secondary of 20 (TSO or ISPF 3.2). Use one of these ways:

#deflist(width: 1.6in,
  [An FTPD that already runs, port 2121, no pre-allocation],
  [Log in, switch to binary and put each file. FTPD creates the data set. A
   #cmd("quote site") with #cmd("RECFM=FB LRECL=80 BLKSIZE=3120") is accepted,
   and you do not need it: a new sequential data set gets these attributes
   from the defaults of the server.],
  [mvsMF with zowe, pre-allocate],
  [#cmd("zowe zos-files upload file-to-data-set") with the option
   #cmd("--binary"), once for each file.],
  [Another FTP server, pre-allocate],
  [Switch to binary and put each file into the existing data set. The
   #cmd("SITE") keywords for data set attributes differ between the TCP/IP
   stacks of MVS 3.8j and are not the z/OS ones, so pre-allocating is the safe
   way.],
  [#cmd("IND$FILE") of a 3270 emulator, no pre-allocation],
  [Use the file transfer of the emulator in binary mode, with the attributes
   above. The syntax of the options depends on the emulator.],
)

An example with an FTPD that is already running:

#screen(```
ftp -P 2121 your-mvs-host
> binary
> put ftpd-1.3.0-load.xmit    'IBMUSER.FTPD.LOAD.XMIT'
> put ftpd-1.3.0-samplib.xmit 'IBMUSER.FTPD.SAMP.XMIT'
> quit
```)

If a file arrives in text mode, the start of FTPD from the freshly installed
library ends with an S106 abend (@ug-install-trouble).

== Allocating the Product Data Sets <ug-install-alloc>

#idx("alloc.jcl")
Submit #cmd("ftpd-1.3.0-alloc.jcl") as it is. The only things worth a change
are the #cmd("UNIT=SYSDA") and the space on each DD statement (and the volume,
if you use an APF entry, @ug-install-auth).

The job creates #cmd("FTPD.LINKLIB") and #cmd("FTPD.AFTPDLOD") and nothing
else. The libraries that the next step receives into are not allocated
here, on purpose: TSO RECEIVE creates its own target and refuses to merge
into an existing data set. The expected result is #cmd("COND CODE 0000").

#note[Run this job once for the life of the system, not once for each
release. The names carry no version, so a later upgrade of a 1.2.0 that SMP
installed finds both data sets and skips this step. Skip it only if
#cmd("FTPD.LINKLIB") and #cmd("FTPD.AFTPDLOD") exist. On MVS/CE they do not:
the FTPD 1.2.0 of the image is not an SMP install (@ug-install-stop), so run
the job there. A second run fails on #cmd("DISP=(NEW,CATLG,DELETE)")
and changes nothing. The job has no DELETE step on purpose:
#cmd("FTPD.AFTPDLOD") holds the accepted copy of the module, and a re-run that
scratched it would leave the SMP records describing an installation that is no
longer there. To start over, remove FTPD first, libraries included
(@ug-remove).]

== Stopping a Running FTPD <ug-install-stop>

When you upgrade, stop the running FTPD first. This is required. The
#cmd("APPLY") writes into #cmd("FTPD.LINKLIB"), and since 1.1.0 that is the
same data set every release uses, so it is the one that a running FTPD loads
from. Replacing it under a live started task risks a wild load.

#screen(```
P FTPD
```)

*MVS/CE ships with an FTPD 1.2.0 that is already running.* The image put it
there; it was not installed through SMP, and the started task runs from
#cmd("MVSLOVER.FTPD.LOADLIB"). Stop it too: a new FTPD started
beside it ends with

#screen(```
FTPD002E FTPD IS ALREADY ACTIVE ON PORT 2121, THIS INSTANCE ENDS
FTPD091E INITIALIZATION FAILED RC=8
```)

and starts only after #cmd("P FTPD").

On MVS/CE this is a first installation, as far as SMP and the data sets go.
The data sets #cmd("FTPD.*") do not exist (#cmd("LISTCAT LEVEL(FTPD)") finds
none), so the allocation job (@ug-install-alloc) is not optional there. Because
SMP never saw that 1.2.0, #cmd("TFTP120") is not in its inventory, and the
#cmd("APPLY") ends with #cmd("COND CODE 0000"), not with the 0004 that an
upgrade gives (@ug-install-good). The old #cmd("MVSLOVER.FTPD.LOADLIB") stays
where it is; the procedure of @ug-install-proc replaces the one that names it.

== Running the Install Job <ug-install-job>

#idx("inst.jcl")
Open #cmd("ftpd-1.3.0-inst.jcl") and replace the two placeholder data set
names with the names you sent in @ug-install-upload:

#screen(```
  RECEIVE INDSN('CHANGE.ME.FTPDLOAD') -      <- IBMUSER.FTPD.LOAD.XMIT
  RECEIVE INDSN('CHANGE.ME.SAMPLIB') -       <- IBMUSER.FTPD.SAMP.XMIT
```)

On most systems these are the only lines to change. On MVS/CE you can put the
two receive targets on the public pack explicitly, by adding a
#cmd("VOLUME('PUB000')") operand to both #cmd("DATASET()") operands (the steps
#cmd("RECV1") and #cmd("RECV2")). A stock MVS/CE ends the job with return code
0 without it and the data sets land on #cmd("PUB000") anyway; the operand only
removes the dependence on that default.

#screen(```
  RECEIVE INDSN('IBMUSER.FTPD.LOAD.XMIT') -
    DATASET('FTPD.FTPDLOAD') VOLUME('PUB000')
  RECEIVE INDSN('IBMUSER.FTPD.SAMP.XMIT') -
    DATASET('FTPD.SAMPLIB') VOLUME('PUB000')
```)

Submit the job. It has eight steps, and each runs only if the one before it
ended well, so the job stops at the first failure. The SYSMOD is inline in the
job; there is no third file to send.

#tab(caption: [The steps of the install job])[
  #table(columns: (1.1in, 1fr),
    [Step], [What it does],
    [#cmd("DELOLD")], [Scratches the targets of the RECEIVE, so that the job
      can be run again.],
    [#cmd("RECV1")], [Receives the load XMIT into #cmd("FTPD.FTPDLOAD"), a
      staging library.],
    [#cmd("RECV2")], [Receives the samplib XMIT into #cmd("FTPD.SAMPLIB").],
    [#cmd("RECV")], [Receives the SYSMOD into the SMP inventory.],
    [#cmd("APPLYCHK")], [A dry run. The #cmd("APPLY") runs only if this one
      ends with RC 0.],
    [#cmd("APPLY")], [Copies the load module into #cmd("FTPD.LINKLIB").],
    [#cmd("ACCEPT")], [Copies the module into #cmd("FTPD.AFTPDLOD"), the base
      that the #cmd("RESTORE") of a later PTF returns to. It also closes
      #cmd("RESTORE") and #cmd("REJECT") for this release itself
      (@ug-remove-why).],
    [#cmd("CLEANUP")], [Scratches the staging library, which is spent now.],
  )
]

=== What a Good Run Looks Like <ug-install-good>

On a new installation, every step ends with #cmd("COND CODE 0000"). The
screens below are the SMP output of a new installation of release 1.3.0,
measured on a test system. The #cmd("RECV") step receives the SYSMOD:

#screen(raw(read("../ex/ug-install/receive.txt")))

In the output of the #cmd("APPLY") step, #cmd("HMA2380") appears once for the
module and the step ends with return code 0:

#screen(raw(read("../ex/ug-install/apply-new.txt")))

The step then prints more reports; the screen shows the first. The status
#cmd("DELETED") for the previous release also appears on a new installation,
because the SYSMOD names it in its #cmd("DELETE") operand. The #cmd("ACCEPT")
step has the same messages for the library #cmd("AFTPDLOD"):

#screen(raw(read("../ex/ug-install/accept-new.txt")))

*On an upgrade the #cmd("APPLY") ends with #cmd("COND CODE 0004") instead, and
that is correct.* The message #cmd("HMA2461") says that the deleted level has
no backup entry to keep; it never will, because it was deleted rather than
superseded:

#screen(```
HMA2380 COPY SUCCESSFUL - MOD=FTPD - LMOD=FTPD - LIBRARY=LINKLIB - ...
HMA2270 APPLY PROCESSING SUCCESSFULLY COMPLETED FOR SYSMOD TFTP130
HMA2461 SYSMOD TFTP120 NOT FOUND ON SMPSCDS LIBRARY
HMA2050 APPLY PROCESSING COMPLETED - HIGHEST RETURN CODE IS 04
```)

#cmd("HMA2270") is the line that says the #cmd("APPLY") worked; the return
code is not. The install job expects the 0004: its #cmd("ACCEPT") step is
gated by #cmd("COND=(4,LT,APPLY.HMASMP)") and not by #cmd("COND=(0,NE,...)"),
so return code 04 passes and anything worse still stops the job. A new
installation has nothing to delete and ends with 00.

SMP copies the module and does not link it again, so the link attributes
(among them AC(1)) are exactly those of the build.

=== List the Members <ug-install-verify>

#idx("verification")
The return codes show that the job ran. They do not show that the work
happened. #cmd("HMA2380") is the message that separates a real installation
from one that copied nothing: if it is not in the job log, nothing was
installed, whatever the condition codes say. SMP also names a library by its
ddname, and a ddname says nothing about the data set behind it. An
installation that owns none of its elements ends with RC 00 in every step and
an empty library. So list the members, and take that as the result. ISPF 3.4
does it, or submit this job:

#screen(```
//FTPDLIST JOB (SYS),'FTPD MEMBERS',
//             CLASS=A,MSGCLASS=H,MSGLEVEL=(1,1)
//MEMBERS EXEC PGM=IKJEFT01
//SYSTSPRT DD  SYSOUT=*
//SYSTSIN  DD  *
 LISTDS 'FTPD.LINKLIB' MEMBERS
 LISTDS 'FTPD.SAMPLIB' MEMBERS
/*
//
```)

The job lists the directory:

#screen(raw(read("../ex/ug-install/members.txt")))

The member #cmd("FTPD") must be there, and #cmd("FTPD.SAMPLIB") must list
#cmd("FTPD") and #cmd("FTPDPRM0"). The volume depends on your system.

If the list is empty although the job said success, the #cmd("APPLY") did not
print #cmd("HMA2380"). Another SYSMOD owns the module name in the SMP
inventory. Each release deletes only the release just before it, so the owner
goes away with the installation only if you come from that release. The usual
cause is a skipped release (@ug-install-upgrade). The command
#cmd("LIST CDS MOD(FTPD) .") names the owner.

=== MVS/CE: When the Allocation Job Was Skipped <ug-install-ce-skip>

#idx("MVS/CE", "allocation job")
On MVS/CE no data set #cmd("FTPD.*") exists before the first installation. If
you skip the allocation job (@ug-install-alloc), the job gets as far as
#cmd("APPLYCHK") and then ends in a JCL error, because
#cmd("FTPD.LINKLIB") is not there. By then the step #cmd("RECV") has run, so
the SYSMOD is in the SMP inventory:

#screen(raw(read("../ex/ug-install/ce-jclerr.txt")))

Run the allocation job and submit the install job again. This time #cmd("RECV")
ends with #cmd("COND CODE 0008"), because the SYSMOD is already received. The
job skips the dry run, and #cmd("APPLY") and #cmd("ACCEPT") end with 0000:

#screen(raw(read("../ex/ug-install/ce-rerun.txt")))

The installation is complete. #cmd("HMA2380") and #cmd("HMA2270") in the output
of the #cmd("APPLY") say so (@ug-install-verify). The only thing missing is the
dry run of that attempt. If you run the allocation job first, none of this
happens.

== Procedures and Configuration <ug-install-proc>

#idx("procedure")
#idx("PARMLIB")
SMP does not touch your procedure library or your PARMLIB. The product owns
the patterns, and your system owns the copies. If SMP owned the running
procedure, every change you made to it would be replaced by the next update.
So this step is yours, and it is the one place where you must read what you
copy.

Copy these members from #cmd("FTPD.SAMPLIB"):

#tab(caption: [The members of the sample library])[
  #table(columns: (1.2in, 1.9in, 1fr),
    [Member], [Copy it to], [Change],
    [#cmd("FTPD")], [a procedure library in the started-task concatenation,
      usually #cmd("SYS2.PROCLIB")],
      [Usually nothing: #cmd("STEPLIB") names #cmd("FTPD.LINKLIB").],
    [#cmd("FTPDPRM0")], [a PARMLIB], [Yes, see below.],
  )
]

Look at the master JCL of your system if you are not sure which libraries the
started tasks search.

On MVS/CE the library #cmd("SYS2.PROCLIB") already has a member #cmd("FTPD"):
the procedure of the image's FTPD 1.2.0, whose #cmd("STEPLIB") is
#cmd("MVSLOVER.FTPD.LOADLIB"). Replace it with the member from
#cmd("FTPD.SAMPLIB"). Otherwise the started task keeps running the old module.

=== Which PARMLIB <ug-install-parmlib>

The procedure #cmd("FTPD") has two symbolic parameters. #cmd("D") names the
PARMLIB and defaults to #cmd("SYS2.PARMLIB"). #cmd("M") names the member and
defaults to #cmd("FTPDPRM0"). A second configuration can live next to the
first as another member, chosen with #cmd("M=").

On TK5 the data set #cmd("SYS2.PARMLIB") does not exist, and the
#cmd("SYS1.PARMLIB") of TK5 has no room for another member. A plain copy of
#cmd("FTPDPRM0") into it ends with the abend SD37, the out-of-space abend of
the system:

#screen(raw(read("../ex/ug-install/parmlib-d37.txt")))

There are two ways out. Use one of them.

*Compress #cmd("SYS1.PARMLIB"), then copy.* The compress gives back the space
that replaced members left behind. It needs the data set exclusively
(#cmd("DISP=OLD")), so nobody may be using it at the time:

#screen(raw(read("../ex/ug-install/parmlib-compress.txt")))

The job ends with #cmd("COND CODE 0000") and prints #cmd("IEB161I COMPRESS TO
BE DONE USING INDD NAMED PARMLIB"). Then run the copy of #cmd("FTPDPRM0") again,
from #cmd("FTPD.SAMPLIB(FTPDPRM0)") to #cmd("SYS1.PARMLIB(FTPDPRM0)"). On a TK5
that had ended with #cmd("D37-04"), it then ended with 0000. Either change the
#cmd("D=") in your copy of the procedure to #cmd("SYS1.PARMLIB") or override it
when you start:

#screen(```
/S FTPD,D='SYS1.PARMLIB'
```)

*Use a PARMLIB of your own and leave #cmd("SYS1.PARMLIB") alone.* Allocate a
partitioned data set, copy the member into it with #cmd("IEBGENER"), and name
the data set with #cmd("D="). Use your own high-level qualifier instead of
#cmd("HERC01"):

#screen(raw(read("../ex/ug-install/parmlib-private.txt")))

#screen(```
/S FTPD,D='HERC01.PARMLIB'
```)

Put the same #cmd("D=") into your copy of the procedure if the plain
#cmd("/S FTPD") should find it. An #cmd("IEBCOPY") from #cmd("SYS1.PARMLIB") into
a library with a larger block size failed on the test system (#cmd("IEB139I"),
#cmd("IEB100I"), return code 8), which is why the job copies with
#cmd("IEBGENER").

On MVS/CE the default is right, because #cmd("SYS2.PARMLIB") exists there.

=== The Procedure and JES <ug-install-jes>

The JES view (@ug-jes) reads the JES2 checkpoint and the spool. FTPD
finds both itself: the checkpoint through the catalog (or, if it is not
cataloged, by looking at the online volumes), and the spool from the volume
that the checkpoint names. The procedure therefore has no #cmd("HASPCKPT") and
#cmd("HASPACE1") DD statements, and for a standard JES2 nothing has to be
changed. Names come from #cmd("DSNPRFX") of JES2, which is #cmd("SYS1") unless
your site changed it. Add the DD statements only when your JES2 uses another
prefix, or when the checkpoint is neither cataloged nor on an online volume:

#screen(```
//HASPCKPT DD  DISP=SHR,DSN=SYS1.HASPCKPT,UNIT=unit,VOL=SER=volser
//HASPACE1 DD  DISP=SHR,DSN=SYS1.HASPACE,UNIT=unit,VOL=SER=volser
```)

Each DD overrides the search for its own data set, so you add only the one that
needs it. Use the unit and the volume of your system, with the data set name as
JES2 names it. Procedures of earlier releases carry both with the values of the
development system (#cmd("UNIT=3350"), #cmd("VOL=SER=MVS000") and
#cmd("SPOOL1")). On any other system, delete them or correct them: a DD with a
wrong data set name or volume makes the JES commands fail with #cmd("451"),
while a DD that only leaves out #cmd("UNIT") and #cmd("VOL") works when the data
set is cataloged.

=== Edit FTPDPRM0 <ug-install-prm>

The member that comes with FTPD is a working default, and three settings are
worth a look before the first start:

- #cmd("SRVPORT") is 2121, not 21, so nothing else on the system has to move
  out of the way.
- #cmd("PASVPORTS") must be a range that your firewall or NAT lets through, or
  a passive transfer stalls after the reply 227.
- #cmd("DEFPRIMARY"), #cmd("DEFSECONDARY") and #cmd("DEFSPACETYPE") decide how
  much space a data set gets when a client creates one. The shipped
  #cmd("TRACK 10,5") caps an upload at 85 tracks, roughly 4 MB at FB/80/3120,
  and a larger upload ends in an x37 abend (@ug-data-space). On a system that
  receives large files use #cmd("DEFSPACETYPE=CYLINDER"),
  #cmd("DEFPRIMARY=10") and #cmd("DEFSECONDARY=10").

Every parameter is described in @ug-ops-config and in full in the _FTPD
Reference_. A #cmd("\#") in the first non-blank column makes a line a comment;
a #cmd("\#") anywhere else is data.

== Setting Up RAKF <ug-install-rakfsetup>

#idx("RAKF")
Without this, FTPD starts and refuses every login. @ug-security explains the
two pieces; the short version for the installation:

+ Give the started task #cmd("FTPD") a low-privilege identity in RAKF. FTPD does
  not log on to one itself: it keeps the identity that RAKF assigned when the
  task was created. On a RAKF that assigns an identity by the name of the
  started task, map the name #cmd("FTPD") to a user ID with Operations
  authority N. On any other RAKF the task runs as #cmd("STC") in the group
  #cmd("STCGROUP"), which has Operations authority. #cmd("FTPD004I") at the
  start names the identity that you got (@ug-security-stc).

  RAKF 2.0.0 assigns by that name. The mapping is a user ID #cmd("FTPD") in
  #cmd("SYS1.SECURE.CNTL(USERS)"), and the group of that user must be allowed
  READ on the profile #cmd("SVC244") in the class #cmd("FACILITY"). FTPD asks
  for its authority with SVC 244 under the identity of the started task
  (@ug-install-auth). If the group is not allowed, FTPD starts and reports
  #cmd("FTPD003W APF SETUP FAILED"). In the table of profiles that comes with
  RAKF 2.0.0 the groups #cmd("ADMIN"), #cmd("STARTED") and #cmd("STCGROUP")
  are allowed, and the seed files of RAKF put the user #cmd("FTPD") into
  #cmd("STARTED"), a group that has nothing else. Do not use #cmd("STCGROUP"):
  in the same table it has ALTER on all data sets and volumes, which is the
  broad authority that the note below rules out. A group of your own needs its
  own permission for #cmd("SVC244"): #cmd("PERMIT SVC244 CLASS(FACILITY) ID(group) ACCESS(READ)").
  Create the user with #cmd("ADDUSER FTPD PASSWORD(pw) DFLTGRP(STARTED)") and
  without #cmd("OPERATIONS"), and show it with #cmd("LISTUSER FTPD"). Your
  table of profiles is the one that counts.

  #note[*RAKF 2.0.0: SPECIAL.* #cmd("ADDUSER"), #cmd("PERMIT") and the other
  RAKF commands run only for a user with the SPECIAL authority. It is the
  flag #cmd("Y") in column 30 of the lines of that user in
  #cmd("SYS1.SECURE.CNTL(USERS)"), and a blank column means no. A table of
  users that you keep from an earlier RAKF has nothing in column 30. Put the
  #cmd("Y") on every line of your administrator, then reload with
  #cmd("S RAKF"). The commands cannot set it for you, because they need it.
  The administrator must also be connected to the group #cmd("RAKFADM").]
+ Define the profile #cmd("FTPAUTH") in the class #cmd("FACILITY"). It
  controls who may use FTP at all (@ug-security-ftpauth). Use the RAKF
  commands #cmd("RDEFINE") and #cmd("PERMIT") in a TSO session or a TSO batch
  job of the administrator (SPECIAL authority and group #cmd("RAKFADM"), see the
  note above), each
  command starting in column 1:

  #note[*Do not edit #cmd("PROFILES") by hand; RAKF's maintainer recommends
  the commands.* They keep the file sorted, refuse a group line that has no
  universal line, and do not write a profile twice. A hand-edited file that
  is wrong aborts the reload (#cmd("U0300"), #cmd("U0400"), #cmd("U0500")) and,
  on RAKF 2.0.0, keeps JES2 from starting at the next IPL.]

  You need not look first whether the profile is there. MVS/CE ships it with
  the universal entry #cmd("NONE") and the group #cmd("ADMIN") at
  #cmd("READ"). On such a system #cmd("RDEFINE") answers that the profile is
  already defined and changes nothing, and #cmd("PERMIT") adds the line for
  your group (#cmd("USER") for every ordinary user, or your own group):

#screen(raw(read("../ex/ug-install/ftpauth-commands.txt"), block: true))

+ Reload the tables. #cmd("S RAKFPROF") reloads the profiles and
  #cmd("S RAKF") reloads the users and the profiles. After the reload the
  console shows #cmd("RAKFPROF7  RAKF PROFILES UPDATED").

The profile now has these lines in #cmd("SYS1.SECURE.CNTL(PROFILES)"). They
are what the commands wrote, shown so that you can check the result. The lines
are column-positioned: the class in columns 1 to 8, the resource from column 9,
the group in columns 53 to 60 and the permission from column 61.

#screen(```
FACILITYFTPAUTH                                             NONE
FACILITYFTPAUTH                                     ADMIN   READ
FACILITYFTPAUTH                                     USER    READ
```)

`docs/FTPD_RAKF_SETUP.md` in the FTPD (ftpd) repository keeps the layout of
the file as a reference for reading and repairing it. A
second universal line for #cmd("FTPAUTH") is what a hand-edited file gets
wrong: the reload ends with #cmd("RAKFPROF3 I/P RECORDS OUT OF ORDER") and the
abend U0300.

FTPD asks RAKF for READ on #cmd("FTPAUTH"). If no profile covers it, RAKF
answers "not protected" (return code 4), and FTPD lets every authenticated user
in.

#note[*The identity of the started task has to stay least-privilege.* Give it
no broad #cmd("ID(FTPD) ACCESS(ALTER)") profiles for data sets. The access of
a user is checked against the identity of that user, and the recovery after an
abend can for a moment put a concurrent session on the identity of the started
task. With a low identity the switch can only lose authority; with a broad one
it would be fail-open (@ug-security-stc).]

*Upgrading from 1.2.0 or earlier.* Those releases logged on as #cmd("FTPD") in
the group #cmd("USER") at the start, whatever RAKF assigned. FTPD 1.3.0 does
not. On a system whose RAKF has no mapping by the name of the started task,
such as TK5 before Update 6, the task now runs as #cmd("STC") in
#cmd("STCGROUP") again, with Operations authority. The line for #cmd("FTPD")
in the table of users is no longer used. On RAKF 2.0.0 the line is the
mapping again, and its group decides the identity of the started task. The
group #cmd("USER") of the FTPD 1.2.0 guide is not one that the profile
#cmd("SVC244") allows in the table that comes with RAKF 2.0.0. FTPD 1.2.0
asks for SVC 244 before it logs on as #cmd("FTPD") in #cmd("USER"), so the
group of the line is the group that is checked there too.

== Starting FTPD and Checking the Start <ug-install-start>

#idx("start")
Start FTPD from the console:

#screen(```
/S FTPD                      default member (FTPDPRM0)
/S FTPD,M=FTPDPRM1           another member
/S FTPD,D='MY.PARMLIB'       another PARMLIB, needed on TK5
```)

On MVS/CE, stop the FTPD 1.2.0 that comes with the image first
(@ug-install-stop); otherwise this start ends with #cmd("FTPD002E") and
#cmd("FTPD091E").

A healthy start ends with #cmd("FTPD001I ... READY"):

#screen(```
FTPD000I FTPD 1.3.0 (A3F2C91) STARTING
FTPD005I LIBC370 2.25.1 (FCDE4E8)
FTPD008I AUTHORIZED BY LIBRARY (MODULE KEY 0)
FTPD004I STC RUNS AS user/group
FTPD054I LISTENING ON ANY PORT 2121
FTPD001I FTPD 1.3.0 READY
```)

Two lines identify the build. #cmd("FTPD000I") gives the version and the commit
that the module was built from. #cmd("FTPD005I") gives the version of LIBC/370
(libc370) that it was linked with. Quote both in a bug report. #cmd("FTPD008I")
says which of the two authorization routes the start took (@ug-install-auth),
and #cmd("FTPD004I") names the identity of the started task
(@ug-install-rakfsetup). A build made from a modified working tree marks its
hash with #cmd("-DIRTY") and adds #cmd("FTPD006W"); a released build never
does. The complete order of the messages of a start is in @ug-ops-msgs.

Then log in from a client with #cmd("ftp -P 2121 your-mvs-host")
(@ug-first-login). One look at #cmd("/F FTPD,CONFIG") after the first start is
worth it: it prints what FTPD parsed, not what the member says, so a typo that
fell back to a default shows there and nowhere else (@ug-ops-con).

== Upgrading from an Earlier Release <ug-install-upgrade>

#idx("upgrade")
Each release deletes only the release just before it. Ask SMP first which
FMID owns the module. Run this under the SMP procedure:

#screen(```
//LIST    EXEC SMPAPP
//SMPCNTL  DD  *
 LIST CDS  MOD(FTPD) .
 LIST ACDS MOD(FTPD) .
/*
```)

#tab(caption: [What the owner of MOD(FTPD) tells you])[
  #table(columns: (1in, 1fr),
    [Owner], [What to do],
    [#cmd("TFTP120")], [An upgrade from 1.2.0. Stop FTPD, skip the allocation
      job (the data sets exist), run the install job and start FTPD. The data
      sets, the procedure, the APF entry and the member of the configuration
      stay.],
    [#cmd("TFTP110")], [1.1.0. Upgrade to 1.2.0 first, with the 1.2.0 package
      from the releases page, then come back here. FTPD 1.3.0 deletes only
      #cmd("TFTP120"), so on a system where #cmd("TFTP110") owns the module the
      install copies nothing and still ends with RC 00.],
    [#cmd("TFTP100")], [1.0.x. Upgrade to 1.1.0 first (@ug-install-from10),
      then to 1.2.0, then come back here.],
    [none], [FTPD is not installed through SMP on this system. This chapter is
      a new installation, allocation job included. This is the case on MVS/CE,
      whose FTPD 1.2.0 comes with the image.],
  )
]

For an upgrade from 1.2.0, the steps are the ones of a new installation without
the allocation: stop FTPD (@ug-install-stop), run the install job
(@ug-install-job) and start FTPD (@ug-install-start). The #cmd("APPLY") ends
with #cmd("COND CODE 0004") (@ug-install-good). The banner must name the new
release and the new C library; #cmd("LIBC370 2.10.3") on the second line means
that the started task still runs 1.2.0. Check the member list and look for
#cmd("HMA2380") in the job log, not the condition codes. This was measured on
a test system that carried 1.2.0, installed through SMP: the #cmd("APPLY")
ended with #cmd("COND CODE 0004") and #cmd("HMA2461 SYSMOD TFTP120 NOT FOUND"),
and the #cmd("ACCEPT") with 0000. If the configuration member of
1.2.0 still has a line for #cmd("JESINTERFACELEVEL"), #cmd("AUTHUSER") or a
volume, the start also prints #cmd("FTPD062W"); the lines have no effect and
can go.

=== From 1.0.x <ug-install-from10>

#idx("upgrade", "from 1.0.x")
1.1.0 changed both the FMID (#cmd("TFTP100") to #cmd("TFTP110")) and the names
of the data sets (#cmd("FTPD.V1R0M1.LINKLIB") and so on to
#cmd("FTPD.LINKLIB")). The FMID is handled for you: the SYSMOD of 1.1.0 carries
a #cmd("DELETE") of #cmd("TFTP100"), and SMP transfers the ownership of the
module during the #cmd("APPLY"). The data sets are not handled for you, and
cannot be.

SMP does not record in which data set an element lives. It records the ddname
that the install job used. Both the old and the new library end in
#cmd("LINKLIB"), so both installs use the ddname #cmd("LINKLIB"), and the
#cmd("APPLY") of 1.1.0 resolves it in its own job: it deletes the old module
from the new library, reports success, copies the new module in, and never
opens the old library. #cmd("FTPD.V1R0M2.LINKLIB") keeps its copy of the old,
still APF-authorized #cmd("FTPD") module, and nothing in the job log mentions
it. Measured on a test system with throwaway ids and a throwaway module name,
in this shape: the new library held the module, and the old target library
still held its own copy. The order of the procedure matters, because the old
libraries are the way back until the banner says that the new release runs:

+ Stop FTPD.
+ Run the allocation job and the install job of 1.1.0. No UCLIN, nothing to
  remove first.
+ Repoint the #cmd("STEPLIB") of the started task procedure to
  #cmd("FTPD.LINKLIB"). This decides whether you run 1.1.0 at all: while the
  old library exists it still holds a working module, and FTPD starts, serves
  and logs no error. It is simply the old one.
+ Replace the APF entry, if you use one, by editing the old #cmd("IEAAPF00")
  line into the new one. The list is read at the IPL only, so until then FTPD
  authorizes through the SVC. The old name stays authorized until that IPL; do
  not reuse it for anything before.
+ Start FTPD and check the banner. #cmd("FTPD005I") must say
  #cmd("LIBC370 1.0.6"). #cmd("LIBC370 1.0.4") means that the started task
  still loads the 1.0.2 module. Stop here if it does: the old libraries are
  untouched.
+ Scratch the old libraries, and only now. Their names carry the patch level
  of what you installed, so look them up (ISPF 3.4 on #cmd("FTPD.*"), or
  #cmd("LISTCAT LEVEL(FTPD)")). Expect three: #cmd("FTPD.V1R0M?.LINKLIB"),
  #cmd("FTPD.V1R0M?.AFTPDLOD") and #cmd("FTPD.V1R0M?.SAMPLIB"). SMP does not
  know them any more, so nothing but you will remove them.

== When Something Goes Wrong <ug-install-trouble>

#idx("troubleshooting", "installation")
#tab(caption: [Installation problems])[
  #table(columns: (1.9in, 1fr),
    [Symptom], [Likely cause],
    [The allocation job fails, the data set exists], [It was run before, or
      #cmd("FTPD.*") exists from an SMP install of 1.2.0. Do not force it
      (@ug-install-alloc).],
    [The install job ends in a JCL error at #cmd("APPLYCHK"): #cmd("IEF212I")
      #cmd("LINKLIB - DATA SET NOT FOUND")], [#cmd("FTPD.LINKLIB") does not
      exist: the allocation job was skipped. This is the MVS/CE case, where the
      image's FTPD is not an SMP install. Run the allocation job and submit the
      install job again (@ug-install-ce-skip).],
    [After the allocation job, #cmd("RECV") ends with #cmd("COND CODE 0008"):
      #cmd("HMA3862 SYSMOD TFTP130 ALREADY RECEIVED")], [The earlier run
      received the SYSMOD before it stopped. #cmd("APPLYCHK") is skipped,
      #cmd("APPLY") and #cmd("ACCEPT") end with 0000. The installation is
      complete; check #cmd("HMA2380") (@ug-install-ce-skip).],
    [#cmd("IEC031I D37-04 ... SYS1.PARMLIB"), abend SD37, while copying
      #cmd("FTPDPRM0") (TK5)], [#cmd("SYS1.PARMLIB") is full. Compress it, or
      use a PARMLIB of your own (@ug-install-parmlib).],
    [#cmd("RECV1") or #cmd("RECV2") fails, the target exists], [Something else
      allocated it. RECEIVE refuses to merge; scratch it and run the job again.],
    [#cmd("APPLYCHK") ends non-zero, #cmd("APPLY") is skipped], [Read the SMP
      output. The check exists to stop before anything is written. A missing DD
      statement is the usual cause.],
    [SMP reports success, but the module is not where you expected], [A ddname
      says nothing about the data set behind it. Check the JCL, then look at
      the library itself (@ug-install-verify).],
    [#cmd("S806") at the start], [#cmd("STEPLIB") in the procedure does not
      name the library that the #cmd("APPLY") wrote to.],
    [The start is rejected, procedure not found], [The procedure is not copied
      into a procedure library of the started tasks.],
    [#cmd("S106") at the start of a new library], [The XMIT was sent in text
      mode. Send it again in binary and run the install job again.],
    [#cmd("S0C4") at the start, no #cmd("FTPD000I")], [FTPD 1.0.0 started from
      an APF-authorized library. Upgrade to 1.0.1, or take the APF entry out
      (@ug-install-auth).],
    [#cmd("FTPD003W APF SETUP FAILED"), and every #cmd("PASS") gets
      #cmd("530 Login not possible: FTPD is not APF authorized")], [No RAKF, so no SVC 244, and no APF
      entry (@ug-install-auth). Nobody can log in until this is fixed.],
    [#cmd("FTPD002E FTPD IS ALREADY ACTIVE")], [An older instance is up.
      #cmd("P FTPD") first.],
    [#cmd("530") for a valid user], [The RAKF user or password is wrong, or the
      profile #cmd("FTPAUTH") denies the user (@ug-security).],
    [#cmd("451") on every JES command], [A #cmd("HASPCKPT") or #cmd("HASPACE1")
      DD in the procedure names the wrong data set or volume
      (@ug-install-jes).],
    [#cmd("550 UFS service not available")], [The UFSD started task is not
      running. Installed is not enough (@ug-install-ufsd).],
  )
]
