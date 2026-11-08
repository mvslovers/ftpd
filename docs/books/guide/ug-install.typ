#import "../bookmaster/bookmaster.typ": *

= Installing FTPD <ug-install>

#idx("installation")
This chapter installs FTPD 1.3.4 on an MVS 3.8j system (TK4-, TK5, MVS/CE or
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
release. After an upgrade SMP refuses a #cmd("RESTORE") of the new level while
it is only applied (@ug-install-upgrade), and it refuses it for an accepted
function in every case (@ug-remove-why). To take FTPD out of the system, follow
@ug-remove.

== What Is in the Package <ug-install-files>

#idx("package", "contents")
The package #cmd("ftpd-1.3.4-dist.zip") (or #cmd("ftpd-1.3.4-dist.tar.gz")) has
six files. In their names, 1.3.4 is the release.

#tab(caption: [The files of the installation package])[
  #table(columns: (2.7in, 1fr),
    [File], [What it is],
    [#cmd("README.md")], [The installation guide as a text file.],
    [#cmd("ftpd-1.3.4-load.xmit")], [The load module, in a form that TSO
      RECEIVE reads.],
    [#cmd("ftpd-1.3.4-samplib.xmit")], [The sample library: the procedure of
      the started task and a configuration pattern.],
    [#cmd("ftpd-1.3.4-alloc.jcl")], [Job #cmd("FTPDALC"): allocates the data
      sets SMP installs into. Run it once, for the first installation, not for
      an upgrade.],
    [#cmd("ftpd-1.3.4-inst.jcl")], [Job #cmd("FTPDINS"): the steps
      #cmd("DELOLD"), #cmd("RECV1"), #cmd("RECV2"), #cmd("RECV") (RECEIVE),
      #cmd("APPLYCHK") (APPLY CHECK) and #cmd("APPLY"). It ends after the
      #cmd("APPLY"): the function is applied, not accepted.],
    [#cmd("ftpd-1.3.4-accept.jcl")], [Job #cmd("FTPDACC"): the steps
      #cmd("GUARD"), #cmd("NOTAPPL"), #cmd("ACCEPT") and #cmd("CLEANUP"). Submit
      it when you have tried the product.],
  )
]

There are three jobs, and you run them in this order: #cmd("FTPDALC") once,
#cmd("FTPDINS"), and later #cmd("FTPDACC").

FTPD is a single load module, #cmd("FTPD"). The sample library holds two
members: #cmd("FTPD") is the procedure of the started task and
#cmd("FTPDPRM0") is the configuration pattern. This is where everything ends up:

#deflist(width: 1.6in,
  [#cmd("FTPD.LINKLIB")], [The load module. The #cmd("STEPLIB") points here.],
  [#cmd("FTPD.SAMPLIB")], [The patterns that you copy in @ug-install-proc.],
  [#cmd("FTPD.AFTPDLOD")], [The distribution library of SMP, the base that a
    #cmd("RESTORE") returns to. It is empty after the install job and filled by
    the accept job. It is not a way back to the previous release
    (@ug-remove-why).],
  [#cmd("FTPD.FTPDLOAD")], [The staging library that the load module is received
    into. The accept job scratches it.],
)

*The install job ends after the APPLY, on purpose.* ACCEPT is the point without
a way back: SMP refuses #cmd("RESTORE") and #cmd("REJECT") for an accepted FMID
(@ug-remove-why). The header of the install job says that a #cmd("RESTORE") can
take the function out again until the ACCEPT. That holds for a first
installation. After an upgrade a #cmd("RESTORE") is refused
(@ug-install-upgrade). So the install job does not make the decision for you:
try the product first, then submit #cmd("FTPDACC")
(@ug-install-accept).

#tab(caption: [What the jobs leave behind])[
  #table(columns: (1.2in, 1fr, 1fr, 1fr),
    [], [After #cmd("FTPDINS"), first installation], [After #cmd("FTPDINS"),
      upgrade], [After #cmd("FTPDACC")],
    [Job end], [CC 0000: every step RC 00], [CC 0004: #cmd("DELOLD"),
      #cmd("RECV1"), #cmd("RECV2"), #cmd("RECV"), #cmd("APPLYCHK") end RC 00,
      #cmd("APPLY") ends RC 04 (#cmd("HMA2461"), @ug-install-upgrade)],
      [CC 0000: #cmd("GUARD"), #cmd("ACCEPT"), #cmd("CLEANUP") end RC 00,
      #cmd("NOTAPPL") is not run],
    [#cmd("TFTP134") in the target zone (CDS)], [#cmd("REC APP")],
      [#cmd("REC APP")], [#cmd("REC APP ACC")],
    [#cmd("TFTP134") in the distribution zone (ACDS)], [not found],
      [not found], [#cmd("REC APP ACC RGN")],
    [#cmd("TFTP133") to #cmd("TFTP130")], [tombstones in the CDS
      (#cmd("DELBY = TFTP134")), none in the ACDS], [#cmd("TFTP133"): tombstone
      in the CDS, in the ACDS still #cmd("REC APP ACC RGN")], [tombstones
      (#cmd("DELBY = TFTP134")) in both zones, no #cmd("RGN") in the ACDS],
    [#cmd("FTPD.LINKLIB")], [#cmd("FTPD")], [the new #cmd("FTPD")],
      [the new #cmd("FTPD")],
    [#cmd("FTPD.AFTPDLOD")], [empty], [still the module of 1.3.3], [the new
      #cmd("FTPD"), the same as in #cmd("FTPD.LINKLIB")],
    [#cmd("FTPD.FTPDLOAD") (staging)], [present], [present], [scratched],
  )
]

This was measured on test containers (TK5 and MVS/CE) with the published package
of 1.3.4, built by mbt 3.0.1. The first column is a first installation, the second
an upgrade over an accepted 1.3.3. The tombstones appear on a first installation
too: the SYSMOD names the four predecessors that it deletes, and SMP records them
whether or not they were ever installed. The first installation was run on TK5 and
on MVS/CE, the upgrade on TK5 (1.3.3, installed from its published package, to
1.3.4), and the accept job on both (on MVS/CE after a first installation, on TK5
after the upgrade). The last column is the same in every case.

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
  them, the staging library #cmd("FTPD.FTPDLOAD"), is scratched again by the
  last step of the accept job\; until you submit that job it stays.

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
talks to across address spaces. FTPD 1.3.4 carries the client library
#cmd("libufs") of UFSD 1.5.2, linked into the load module.

#note[*Install UFSD 1.5.2 for the UFS view.* UFSD 1.5.1 and earlier releases
could give one disk block to two files, or twice to the same file, while a file
was being written, and reported no error. A file written this way holds
foreign data (measured on 1.4.0 and 1.5.1). The fix is in the UFSD server, not
in #cmd("libufs"), so a new FTPD does not help: the server must be upgraded.
UFSD 1.5.2 stops the damage. It does not undo it. A file that was damaged
before stays damaged, so compare such files with a copy, or write them again
after the upgrade. #cmd("F UFSD,REBUILD") is not a repair. The release notes
of UFSD 1.5.2 have the details.]

=== RAKF Is Required for Logins <ug-install-rakf>

#idx("RAKF")
FTPD has no user database of its own. Every #cmd("USER") and #cmd("PASS") is
verified by RAKF, and every data set access is checked against the RAKF class
#cmd("DATASET") under the identity of the user who is logged in
(@ug-security). On a system without RAKF nobody can log in, and no
configuration option turns that off. @ug-install-rakfsetup says what to set
up.

FTPD 1.3.3 does not need RAKF 2.0.0 to run. On a RAKF without the class
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
allocation job of 1.3.4 never lets #cmd("SYSDA") pick: it allocates
#cmd("FTPD.LINKLIB") on the volume that #cmd("TGTVOL") names and
#cmd("FTPD.AFTPDLOD") on #cmd("DLBVOL"). The defaults are #cmd("MVSRES") and
#cmd("SMP000"), the volumes of an MVS/CE system\; on TK5 it is
#cmd("TGTVOL=TK5RES,DLBVOL=TK5DLB") (@ug-install-alloc). The job does not read
the APF list and does not say if the volumes differ. So set #cmd("TGTVOL") to
the volume in the entry. If you do not, you get a library that exists, holds the
right module, passes every check of @ug-install-verify, and is not authorized.
(The allocation job of 1.3.3 and earlier said #cmd("UNIT=SYSDA") and no
#cmd("VOL=SER"), which put the library wherever #cmd("SYSDA") picked.)

Nothing reports the mismatch, because FTPD recovers from it: it falls back to
the SVC and comes up authorized. The only place where the two routes can be
told apart is #cmd("FTPD008I"). So #cmd("AUTHORIZED BY SVC") on a system where
you added an APF entry is not a decision of the site but a symptom. This was
measured on a test system with an #cmd("IEAAPF00") entry for a data set that did
not exist yet and the allocation job of 1.3.3, free to put it anywhere.

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
> put ftpd-1.3.4-load.xmit    'IBMUSER.FTPD.LOAD.XMIT'
> put ftpd-1.3.4-samplib.xmit 'IBMUSER.FTPD.SAMP.XMIT'
> quit
```)

If a file arrives in text mode, the start of FTPD from the freshly installed
library ends with an S106 abend (@ug-install-trouble).

== Allocating the Product Data Sets <ug-install-alloc>

#idx("alloc.jcl")
Submit #cmd("ftpd-1.3.4-alloc.jcl") after you have set the two volumes. The job
allocates every library on a volume you name, never on whatever #cmd("SYSDA")
picks:

#tab(caption: [The volume parameters of the allocation job])[
  #table(columns: (1in, 1fr, 1in),
    [Parameter], [Volume of], [Default],
    [#cmd("TGTVOL")], [The target library #cmd("FTPD.LINKLIB"), the one SMP
      installs into.], [#cmd("MVSRES")],
    [#cmd("DLBVOL")], [The distribution library #cmd("FTPD.AFTPDLOD"), the one
      the ACCEPT fills.], [#cmd("SMP000")],
  )
]

The defaults are the volumes of an MVS/CE system. On TK5 write
#cmd("TGTVOL=TK5RES,DLBVOL=TK5DLB"), either on the #cmd("PROC") statement or on
the statement #cmd("//ALLOC EXEC SMPALLOC,COND=(0,NE,CHECK)") at the end of the
job. Whatever your system uses, the volume must be online. If an
#cmd("IEAAPF00") entry for #cmd("FTPD.LINKLIB") exists, set #cmd("TGTVOL") to the
volume that it names (@ug-install-auth). The space on each DD statement is the
only other thing worth a change.

The job has two steps. #cmd("CHECK") looks in the catalog for the two libraries.
On a first run it prints #cmd("IDC3012I ENTRY ... NOT FOUND") for both, which is
the answer you want, and ends with RC 0. Then #cmd("ALLOC") creates
#cmd("FTPD.LINKLIB") and #cmd("FTPD.AFTPDLOD") and nothing else. The libraries
that the next step receives into are not allocated here, on purpose: TSO RECEIVE
creates its own target and refuses to merge into an existing data set. The
expected result is #cmd("COND CODE 0000").

*A second run does nothing.* If either library is already cataloged,
#cmd("CHECK") lists it and ends with RC 8, #cmd("ALLOC") is not run
(#cmd("IEF202I"), #cmd("IEF272I")) and the job ends with CC 0008. This was
measured on MVS/CE. Without the look, a second run would have ended with RC 0 and
left an uncataloged copy of the library on the other volume.

*A volume that is not mounted holds the job.* It does not fail it. This was
measured on TK5 with the default #cmd("MVSRES"), which TK5 does not have: the job
log shows #cmd("IEF244I ... UNABLE TO ALLOCATE") and the console
#cmd("IEF238D FTPDALC - REPLY DEVICE NAME OR 'CANCEL'"). Reply
#cmd("R nn,CANCEL") (the reply number is on the console line), and the job ends
in a JCL error. Correct the volume and submit the job again.

#note[Run this job once, for the first installation. An upgrade skips it when
the data sets are already there\; on MVS/CE they are not (@ug-install-stop), so
MVS/CE runs it. If you skip it where #cmd("FTPD.LINKLIB") does not exist, the
install job ends in a JCL error (@ug-install-ce-skip). The job has no DELETE step
on purpose. After the accept job, #cmd("FTPD.AFTPDLOD") holds the accepted copy
of the module that SMP keeps\; a re-run that scratched it would leave the SMP
inventory reporting an installation that is no longer on the system, and nothing
would say so. To start over, remove FTPD first, libraries included
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
Open #cmd("ftpd-1.3.4-inst.jcl") and replace the two placeholder data set
names with the names you sent in @ug-install-upload:

#screen(```
  RECEIVE INDSN('CHANGE.ME.FTPDLOAD') -      <- IBMUSER.FTPD.LOAD.XMIT
  RECEIVE INDSN('CHANGE.ME.SAMPLIB') -       <- IBMUSER.FTPD.SAMP.XMIT
```)

On most systems these are the only lines to change. On MVS/CE, name the volume.
TSO RECEIVE allocates #cmd("FTPD.FTPDLOAD") and #cmd("FTPD.SAMPLIB") itself and
lets the system pick the volume. Put the two data sets on the public pack
explicitly: in both #cmd("RECEIVE") commands of the job (the steps #cmd("RECV1")
and #cmd("RECV2")), take the comment marks off the #cmd("VOLUME") line and name
the volume, #cmd("PUB000") on a stock MVS/CE:

#screen(```
  RECEIVE INDSN('IBMUSER.FTPD.LOAD.XMIT') -
    VOLUME('PUB000') -
    DATASET('FTPD.FTPDLOAD')
  RECEIVE INDSN('IBMUSER.FTPD.SAMP.XMIT') -
    VOLUME('PUB000') -
    DATASET('FTPD.SAMPLIB')
```)

The header of the job says that steps 3 and 4 there (the names, and this volume)
are the only edits it needs. On TK5 leave the #cmd("VOLUME") lines alone. This was
measured on MVS/CE with this edit: #cmd("FTPD.FTPDLOAD") and #cmd("FTPD.SAMPLIB")
on #cmd("PUB000"), #cmd("FTPD.LINKLIB") on #cmd("MVSRES") and
#cmd("FTPD.AFTPDLOD") on #cmd("SMP000") (the defaults of the allocation job).
#cmd("FTPD.SAMPLIB") is the library that you keep, so check it with ISPF 3.4
after the job and read the volume from the listing. Without the
#cmd("VOLUME") lines the job was not run on MVS/CE with 1.3.4.

Submit the job. It has six steps, and each runs only if the ones before it ended
well, so the job stops at the first failure and then it ends. The accept job is a
job of its own. The SYSMOD is inline in the job; there is no third file to send.

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
  )
]

At the end of the job, a comment says that the function is applied, not accepted,
and names #cmd("ftpd-1.3.4-accept.jcl") as the next job. Do not submit it yet.
Go on with @ug-install-proc to @ug-install-start, try the product, and then
submit it (@ug-install-accept).

=== What a Good Run Looks Like <ug-install-good>

On a new installation, every step ends with #cmd("COND CODE 0000"). The
screens below are the SMP output of a new installation. They were measured with
the published package of 1.3.4, installed the way this chapter describes (the
allocation job, then the install job), on TK5 and on MVS/CE. All six steps of
the install job ended with #cmd("COND CODE 0000"). The #cmd("RECV") step receives
the SYSMOD:

#screen(raw(read("../ex/ug-install/receive.txt")))

In the output of the #cmd("APPLY") step, #cmd("HMA2380") appears once for the
module and the step ends with return code 0:

#screen(raw(read("../ex/ug-install/apply-new.txt")))

The step then prints more reports; the screen shows the first lines. The status
#cmd("DELETED") for the previous releases also appears on a new installation,
because the SYSMOD names them in its #cmd("DELETE") operand. #cmd("HMA2380") is
the message that separates a real installation from one that copied nothing. If
it is not in the job log, nothing was installed, whatever the condition codes
say.

*On an upgrade the #cmd("APPLY") ends with #cmd("COND CODE 0004") instead, and
that is correct.* The message #cmd("HMA2461") says that the deleted level has
no backup entry to keep. It never will, because it was deleted rather than
superseded:

#screen(raw(read("../ex/ug-install/apply-upgrade.txt")))

The screen is the upgrade from 1.3.3 to 1.3.4, measured with the published
package of 1.3.4 on a TK5 test container with 1.3.3 installed from its own
published package. The install job ended with the return codes #cmd("DELOLD") 0,
#cmd("RECV1") 0, #cmd("RECV2") 0, #cmd("RECV") 0, #cmd("APPLYCHK") 0 and
#cmd("APPLY") 4. The line #cmd("HMA2240") is the old module being deleted from
#cmd("FTPD.LINKLIB") before the new one is copied. #cmd("HMA2270") is the line
that says the #cmd("APPLY") worked\; the return code is not. On the upgrades from
1.3.2, 1.3.1 and 1.3.0 to 1.3.3 (measured with the published package of 1.3.3,
which was one job) the #cmd("APPLY") also ended with #cmd("COND CODE 0004"), and
#cmd("HMA2461") named #cmd("TFTP132"), #cmd("TFTP131") and #cmd("TFTP130").

The accept job expects the 0004: its #cmd("GUARD") step looks for #cmd("TFTP134")
in the target zone and not at the return code of the install job, so the accept
job runs through after CC 0004 (it was run that way). A new installation has
nothing to delete and ends with 00 (measured with the 1.3.4 package on TK5 and
on MVS/CE). On an upgrade, a #cmd("RESTORE") does not take the new level back
(@ug-install-upgrade).

SMP copies the module and does not link it again, so the link attributes
(among them AC(1)) are exactly those of the build.

#note[*Development builds.* A build made from #cmd("main") between two releases
carries a development name and prints it in the banner, for example
#cmd("1.3.4-DEV") for a build made on the way to release 1.3.4. The package of
release 1.3.4 prints #cmd("1.3.4").]

=== List the Members <ug-install-verify>

#idx("verification")
The return codes show that the job ran. They do not show that the work
happened. #cmd("HMA2380") is the message that separates a real installation
from one that copied nothing: if it is not in the job log, nothing was
installed, whatever the condition codes say. SMP also names a library by its
ddname, and a ddname says nothing about the data set behind it. SMP keys the
ownership of an element on #cmd("MOD(name)"), and a SYSMOD that does not own an
element skips it: the element summary then prints

#screen(```
TYPE   NAME      STATUS
MOD    FTPD      NOT SEL
```)

Nothing is copied, and every other line still says success. An installation like
that ends with RC 00 in every step and an empty library. The line for
#cmd("LIBRARY=LINKLIB") is in the #cmd("APPLY") step of #cmd("FTPDINS")\; the one
for #cmd("LIBRARY=AFTPDLOD") is in the #cmd("ACCEPT") step of #cmd("FTPDACC").
(The #cmd("NOT SEL") case was run with the package of 1.3.2, whose install job
also accepted (@ug-remove)\; not with this package.) So list the members, and take
that as the result. ISPF 3.4 does it, or submit this job:

#screen(```
//FTPDLIST JOB (SYS),'FTPD MEMBERS',
//             CLASS=A,MSGCLASS=H,MSGLEVEL=(1,1)
//MEMBERS EXEC PGM=IKJEFT01
//SYSTSPRT DD  SYSOUT=*
//SYSTSIN  DD  *
 LISTDS 'FTPD.LINKLIB' MEMBERS
 LISTDS 'FTPD.AFTPDLOD' MEMBERS
 LISTDS 'FTPD.SAMPLIB' MEMBERS
/*
//
```)

The job lists the directories. The screen shows the first two, after the install
job of a first installation on TK5:

#screen(raw(read("../ex/ug-install/members.txt")))

The member #cmd("FTPD") must be in #cmd("FTPD.LINKLIB"), and
#cmd("FTPD.SAMPLIB") must list #cmd("FTPD") and #cmd("FTPDPRM0"). The volumes
depend on your system. #cmd("FTPD.AFTPDLOD") is empty until the accept job has
run, and holds the member #cmd("FTPD") after it (@ug-install-accept).

If the list is empty although the job said success, the #cmd("APPLY") did not
print #cmd("HMA2380"). Another SYSMOD owns the module name in the SMP
inventory. FTPD 1.3.4 deletes #cmd("TFTP133"), #cmd("TFTP132"),
#cmd("TFTP131") and #cmd("TFTP130"), so the owner goes away with the
installation only if you come from 1.3.0, 1.3.1, 1.3.2 or 1.3.3. The usual cause
is an older release that was not installed first (@ug-install-upgrade). The
command #cmd("LIST CDS MOD(FTPD) .") names the owner.

=== MVS/CE: When the Allocation Job Was Skipped <ug-install-ce-skip>

#idx("MVS/CE", "allocation job")
On MVS/CE no data set #cmd("FTPD.*") exists before the first installation. If
you skip the allocation job (@ug-install-alloc), the job gets as far as
#cmd("APPLYCHK") and then ends in a JCL error, because
#cmd("FTPD.LINKLIB") is not there. By then the step #cmd("RECV") has run, so
the SYSMOD is in the SMP inventory:

#screen(raw(read("../ex/ug-install/ce-jclerr.txt")))

Run the allocation job and submit the install job again. This time #cmd("RECV")
ends with #cmd("COND CODE 0008"), because the SYSMOD is already received.
#cmd("APPLYCHK") and #cmd("APPLY") are not run, and *nothing is installed*. There
is no #cmd("ACCEPT") or #cmd("CLEANUP") in this job:

#screen(raw(read("../ex/ug-install/ce-rerun.txt")))

Take the received SYSMOD out first: #cmd("REJECT S(TFTP134)") (RC 0,
#cmd("HMA2270")). Then submit the install job again. It ends with CC 0000. This
was measured on TK5 and on MVS/CE with the published package of 1.3.4, the first
run ending in the JCL error at #cmd("APPLYCHK") that the screen above shows. If
you run the allocation job first, none of this happens.

The same #cmd("HMA3862") ends a second run of the install job when
#cmd("TFTP134") is already applied: CC 0008, nothing changed. The way out there is
a #cmd("RESTORE") (first installation only, @ug-install-upgrade) or the removal
chapter (@ug-remove).

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

The member that comes with FTPD is a working default, and four settings are
worth a look before the first start:

- #cmd("SRVPORT") is 2121, not 21, so nothing else on the system has to move
  out of the way.
- #cmd("PASVPORTS") must be a range that your firewall or NAT lets through, or
  a passive transfer stalls after the reply 227.
- #cmd("PASVADR") is #cmd("ANY"), which is right when the client reaches the
  host directly. Behind NAT, port forwarding or a container network, set it to
  the address that the client can reach, written as an IPv4 address, not as a
  name, and forward the whole #cmd("PASVPORTS") range to the host. Left at
  #cmd("ANY"), FTPD gives the client a private address, and some clients wait
  30 seconds for it before every transfer (@ug-install-trouble).
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
the group #cmd("USER") at the start, whatever RAKF assigned. FTPD 1.3.3 does
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
FTPD000I FTPD 1.3.4 (B079CED) STARTING
FTPD005I LIBC370 2.29.2 (70E450F)
FTPD008I AUTHORIZED BY SVC (MODULE KEY 8)
FTPD004I STC RUNS AS user/group
RAKF0004 INVALID ATTEMPT TO ACCESS SYSTEM:  USER:?FTPDPRBO
FTPD054I LISTENING ON ANY PORT 2121
FTPD001I FTPD 1.3.4 READY
```)

These lines were measured with the published package of 1.3.4 on MVS/CE, with
FTPD started from the library of the install job, before the accept job. There
the #cmd("FTPD004I") line read #cmd("FTPD/STARTED"), and the #cmd("RAKF0004") line
carried one more character behind the name, as explained below. A start on TK5
was not run with the published package. The version and the hash in the first
line, and the LIBC/370 level and hash in the second, are those of the release you
installed\; a later release prints its own. The identity in the fourth line is the
one that RAKF gave your started task. The test system started by the SVC route, so
#cmd("FTPD008I") says #cmd("SVC (MODULE KEY 8)")\; on a system with the load
library in the APF list it says #cmd("LIBRARY (MODULE KEY 0)")
(@ug-install-auth). If your parameter file still has settings that no longer
have an effect, the start also prints #cmd("FTPD062W") before the line
#cmd("FTPD054I").

*The #cmd("RAKF0004") line is expected.* It comes from RAKF and not from FTPD:
FTPD logs in a user ID that cannot be valid, to see that RAKF refuses it, and
RAKF reports the refusal. One such line per start is part of a healthy start
(@ug-ops-check). The user ID is #cmd("?FTPDPRB"). RAKF may print one more
character behind it (#cmd("?FTPDPRBO") above), so a rule that looks for the line
has to match the prefix. If you see two lines #cmd("FTPD004E") in this place instead,
the check has failed, and nobody can log in until the security tables are
corrected and FTPD is started again (@ug-security-check).

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

== Accepting the Installation <ug-install-accept>

#idx("accept job")
#idx("accept.jcl")
Submit #cmd("ftpd-1.3.4-accept.jcl") (job #cmd("FTPDACC")) when you have tried the
product and want to keep this level. Nothing in it needs editing. It has four
steps and ends with CC 0000 when the function is applied:

#tab(caption: [The steps of the accept job])[
  #table(columns: (1.1in, 1fr),
    [Step], [What it does],
    [#cmd("GUARD")], [#cmd("LIST CDS SYSMOD(TFTP134)"): is the function applied?
      RC 0 if it is, RC 4 if it is not.],
    [#cmd("NOTAPPL")], [Runs only when #cmd("GUARD") did not end with RC 0, and
      ends the job with RC 8.],
    [#cmd("ACCEPT")], [#cmd("ACCEPT S(TFTP134)"): copies the module into
      #cmd("FTPD.AFTPDLOD"), the base that the #cmd("RESTORE") of a later PTF
      returns to.],
    [#cmd("CLEANUP")], [Scratches the staging library #cmd("FTPD.FTPDLOAD"),
      which is spent now.],
  )
]

*This is the point without a way back.* Once #cmd("TFTP134") is accepted, SMP
refuses #cmd("RESTORE") and #cmd("REJECT") for it (@ug-remove-why). Until then, a
first installation can still be taken out again with a #cmd("RESTORE")\; an
upgrade cannot (@ug-install-upgrade).

#cmd("ACCEPT") runs only when #cmd("GUARD") ended with RC 00, and
#cmd("CLEANUP") scratches #cmd("FTPD.FTPDLOAD") only when #cmd("GUARD") and
#cmd("ACCEPT") both ended with RC 00. The gate is the entry of #cmd("TFTP134") in
the target zone, not the return code of the install job: after an upgrade the
install job ends with CC 0004, and the accept job still runs through. The header
of the accept job says to submit it after an install job whose APPLY CHECK and
APPLY ended with RC 0, or with RC 4 for #cmd("HMA2461") (a predecessor in delete)
or #cmd("HMA4441") alone, and not after RC 8 or 12. The #cmd("ACCEPT") step prints
#cmd("HMA2380 COPY SUCCESSFUL ... LIBRARY=AFTPDLOD") for #cmd("FTPD"). These are
the messages to look for after a first installation:

#screen(raw(read("../ex/ug-install/accept-new.txt")))

and the cleanup step prints #cmd("IDC0550I ENTRY (A) FTPD.FTPDLOAD DELETED").
After an upgrade the #cmd("ACCEPT") also prints #cmd("HMA2240"): the module of
1.3.3 that the new one replaces is deleted from #cmd("FTPD.AFTPDLOD"):

#screen(raw(read("../ex/ug-install/accept-upgrade.txt")))

Both screens were measured with the published package of 1.3.4. Then check three
things: the member #cmd("FTPD") is in #cmd("FTPD.AFTPDLOD"), #cmd("FTPD.FTPDLOAD")
is gone, and

#screen(```
//LIST    EXEC SMPAPP
//SMPCNTL  DD  *
 LIST CDS  SYSMOD(TFTP134) .
 LIST ACDS SYSMOD(TFTP134) .
/*
```)

shows #cmd("TFTP134") as #cmd("REC APP ACC") in both zones. Verify by listing
the members of #cmd("FTPD.LINKLIB") and #cmd("FTPD.AFTPDLOD"), never by the
condition codes (@ug-install-verify).

*Submitting the accept job a second time* ends the job with #cmd("JCL ERROR"):
#cmd("IEF212I ... FTPDLOAD - DATA SET NOT FOUND"), because the first run scratched
the staging library. #cmd("GUARD") ends with RC 00 and no step after it runs, so
the second run changes nothing.

*Submitting it when #cmd("TFTP134") is not applied* (the install job never ran,
or the function was taken out again by a #cmd("RESTORE") or by the removal job)
ends #cmd("GUARD") with RC 04, because the entry is not in the target zone.
#cmd("NOTAPPL") then ends with RC 08, and #cmd("ACCEPT") and #cmd("CLEANUP") do
not run. The job ends with CC 0008, and nothing is accepted or scratched. This was
measured after a removal.

*The accept job may run while FTPD is up.* This was measured on MVS/CE: FTPD 1.3.4
was started from the applied-only #cmd("FTPD.LINKLIB"), the accept job ran through
(CC 0000), and #cmd("F FTPD,STATS") afterwards answered
#cmd("FTPD010I STATUS: ACTIVE"). The accept job does not write
#cmd("FTPD.LINKLIB").

If the #cmd("ACCEPT") fails, #cmd("CLEANUP") does not run and nothing is
scratched: fix the cause and submit the job again (that case was not run). If you
decide not to accept, because you took the function out with a #cmd("RESTORE") or
because you keep it applied, scratch the staging library yourself:

#screen(```
//SCRATCH EXEC PGM=IDCAMS
//SYSPRINT DD  SYSOUT=*
//SYSIN    DD  *
  DELETE FTPD.FTPDLOAD NONVSAM SCRATCH PURGE
/*
```)

The install job needs no such scratch before a second run: its #cmd("DELOLD")
step deletes #cmd("FTPD.FTPDLOAD") and #cmd("FTPD.SAMPLIB") itself.

*Not measured:* the accept job after an #cmd("APPLY") that printed
#cmd("NOT SEL")\; a system where #cmd("PURGE=NO") is set in the PTS options\; an
upgrade from 1.3.0, 1.3.1 or 1.3.2 straight to 1.3.4 (only 1.3.3 to 1.3.4 was
run)\; and the accept job on a system where #cmd("TFTP134") is received but not
applied.

== Upgrading from an Earlier Release <ug-install-upgrade>

#idx("upgrade")
FTPD 1.3.4 deletes #cmd("TFTP133"), #cmd("TFTP132"), #cmd("TFTP131") and
#cmd("TFTP130"), so a system on any of 1.3.0, 1.3.1, 1.3.2 and 1.3.3 upgrades
in one step. Ask SMP first
which FMID owns the module. Run this under the SMP procedure:

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
    [#cmd("TFTP133"), #cmd("TFTP132"), #cmd("TFTP131") or #cmd("TFTP130")], [An
      upgrade from 1.3.3, 1.3.2, 1.3.1 or 1.3.0. Stop FTPD, skip the allocation job (the data sets
      exist), run the install job, start FTPD, try the product, then run the accept job. The data sets, the procedure,
      the APF entry and the member of the configuration stay. On 1.3.0 note
      that the client library of UFSD changes (UFSD 1.5.2 or later is needed
      for the UFS view).],
    [#cmd("TFTP120")], [1.2.0. Upgrade to 1.3.0 first, with the 1.3.0 package
      from the releases page, then come back here. FTPD 1.3.4 does not delete
      #cmd("TFTP120"), so on a system where #cmd("TFTP120") owns the module the
      install copies nothing and still ends with RC 00: the #cmd("APPLY")
      prints no #cmd("HMA2380"), and its report lists the module as
      #cmd("NOT SEL"). This was measured with #cmd("TFTP120") as the owner of
      the module and the SYSMOD of 1.3.1, of 1.3.2 and of 1.3.3; it was not
      measured with the SYSMOD of 1.3.4.],
    [#cmd("TFTP110")], [1.1.0. Upgrade to 1.2.0 first, then to 1.3.0, each with
      its package from the releases page, then come back here.],
    [#cmd("TFTP100")], [1.0.x. Upgrade to 1.1.0 first (@ug-install-from10),
      then to 1.2.0 and 1.3.0, then come back here.],
    [none], [FTPD is not installed through SMP on this system. This chapter is
      a new installation, allocation job included. This is the case on MVS/CE,
      whose FTPD 1.2.0 comes with the image.],
  )
]

#cmd("TFTP130"), #cmd("TFTP131"), #cmd("TFTP132") and #cmd("TFTP133") stay in
the SMP zones after the install as entries that only say
#cmd("DELBY = TFTP134") or #cmd("DELBY = TFTP133"), also the ones your system
never had. That is how SMP records a deleted id. They are not an install, and
nothing needs to be done about them.

For an upgrade from 1.3.0, 1.3.1, 1.3.2 or 1.3.3 there are five steps:

+ Stop FTPD (@ug-install-stop).
+ Skip the allocation job, if #cmd("FTPD.LINKLIB") and #cmd("FTPD.AFTPDLOD")
  exist, which they do after an SMP install of 1.3.0, 1.3.1, 1.3.2 or 1.3.3.
  The job allocates them new: its #cmd("CHECK") step looks in the catalog first
  and ends with RC 8 on an existing library, so nothing is allocated twice
  (@ug-install-alloc). #cmd("LISTCAT LEVEL(FTPD)")\; or ISPF 3.4 on
  #cmd("FTPD.*") tells which case you are in.
+ Run the install job (@ug-install-job). It ends with #cmd("COND CODE 0004"):
  the #cmd("APPLY") ends with #cmd("COND CODE 0004") on
  #cmd("HMA2461 SYSMOD TFTP13x NOT FOUND ON SMPSCDS LIBRARY"), where
  #cmd("TFTP13x") is the id that was installed (#cmd("TFTP130"),
  #cmd("TFTP131"), #cmd("TFTP132") or #cmd("TFTP133")). That is the expected
  result of a replacement (@ug-install-good). The job stops after the
  #cmd("APPLY"): the new module is in #cmd("FTPD.LINKLIB"), and
  #cmd("FTPD.AFTPDLOD") still holds the old one.
+ Start FTPD (@ug-install-start) and read the banner. It must name the new
  release: #cmd("FTPD000I FTPD 1.3.4"). #cmd("FTPD 1.3.0"), #cmd("1.3.1"),
  #cmd("1.3.2") or #cmd("1.3.3") on the first line means that the started task
  still runs the old load module. Check the member list of
  #cmd("FTPD.LINKLIB") and look for #cmd("HMA2380") in the job log, not at the
  condition codes alone.
+ Try the product, then run the accept job (@ug-install-accept). It ends with
  #cmd("COND CODE 0000"), scratches #cmd("FTPD.FTPDLOAD") and makes
  #cmd("TFTP134") the base for any later #cmd("RESTORE"). It does not need FTPD
  to be stopped (measured on MVS/CE with FTPD running). Until it has run, the
  system holds #cmd("TFTP134") as applied and #cmd("TFTP133") as the accepted
  level in the distribution zone.

The upgrade to 1.3.4 was measured on a TK5 test container that went through the
upgrade chain 1.2.0, 1.3.0, 1.3.3 and 1.3.4, each package installed through
SMP, the last one with the published package of 1.3.4. The install job ended
with #cmd("COND CODE 0004"): #cmd("DELOLD"), #cmd("RECV1"), #cmd("RECV2"),
#cmd("RECV") and #cmd("APPLYCHK") ended with RC 0, and #cmd("APPLY") with RC 4
and #cmd("HMA2461 SYSMOD TFTP133 NOT FOUND ON SMPSCDS LIBRARY"). The accept job
then ended with #cmd("COND CODE 0000"): #cmd("GUARD"), #cmd("ACCEPT") and
#cmd("CLEANUP") with RC 0, and #cmd("NOTAPPL") not run. The upgrade from 1.3.0,
1.3.1 and 1.3.2 directly to 1.3.4 was not measured\; the SYSMOD of 1.3.4 names
those ids in its delete list all the same.

*Until the accept job has run, a #cmd("RESTORE") does not take the upgrade
back.* The header of the shipped install job says that until the
#cmd("ACCEPT") a #cmd("RESTORE") can still take the function out again. After
an upgrade over an accepted #cmd("TFTP133") that is not so. Measured with
#cmd("RESTORE S(TFTP134)") after the install job of the upgrade above, with the
new module still in #cmd("FTPD.LINKLIB"):

#screen(```
HMA2452 ** SYSMOD TFTP134 SELECTED FOR RESTORE DELETES SYSMODS
HMA3703 ** RESTORE PROCESSING TERMINATED BECAUSE FUNCTION SYSMOD TFTP134 FAILED
```)

The #cmd("RESTORE") ended with RC 12 and changed nothing. So the way back from
an applied #cmd("TFTP134") over #cmd("TFTP133") is not a #cmd("RESTORE").
Either keep #cmd("TFTP134") and accept it, or remove FTPD as the removal
chapter describes (@ug-remove)\; installing the old release afterwards was not
run. On a first install, where nothing was deleted, a #cmd("RESTORE") does take
the function out (measured: RC 0, #cmd("HMA2240") deleted the load module from
#cmd("FTPD.LINKLIB")).

If the configuration member of 1.2.0 still has a line for
#cmd("JESINTERFACELEVEL"), #cmd("AUTHUSER") or a volume, the start also prints
#cmd("FTPD062W"); the lines have no effect and can go.

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
      #cmd("FTPD.*") exists from an SMP install of 1.2.0. The #cmd("CHECK") step
      ends with RC 8 and #cmd("ALLOC") is not run. Do not force it
      (@ug-install-alloc).],
    [The install job ends in a JCL error at #cmd("APPLYCHK"): #cmd("IEF212I")
      #cmd("LINKLIB - DATA SET NOT FOUND")], [#cmd("FTPD.LINKLIB") does not
      exist: the allocation job was skipped. This is the MVS/CE case, where the
      image's FTPD is not an SMP install. Run the allocation job and submit the
      install job again (@ug-install-ce-skip).],
    [After the allocation job, the install job ends with #cmd("COND CODE 0008")
      in #cmd("RECV"): #cmd("HMA3862 SYSMOD TFTP134 ALREADY RECEIVED")], [The
      first run had already received the SYSMOD before it stopped at
      #cmd("APPLYCHK"). The run ends with RC 8 in #cmd("RECV") (#cmd("HMA3862"),
      #cmd("HMA3902"), #cmd("HMA3920"), #cmd("HMA2481")). #cmd("APPLYCHK") and
      #cmd("APPLY") are not run (#cmd("IEF202I")\; and #cmd("IEF272I")), and
      nothing is installed. There is no #cmd("ACCEPT") or #cmd("CLEANUP") in this
      job. Take the received SYSMOD out first with #cmd("REJECT S(TFTP134)")
      (RC 0, #cmd("HMA2270")), then submit the install job again\; it ends with
      #cmd("COND CODE 0000") (@ug-install-ce-skip). The same #cmd("HMA3862")
      ends a second run of the install job when #cmd("TFTP134") is already
      applied: #cmd("COND CODE 0008"), nothing changed. The way out there is a
      #cmd("RESTORE") (first install only, @ug-install-upgrade) or the removal
      chapter (@ug-remove).],
    [The accept job ends in a JCL error: #cmd("IEF212I") #cmd("FTPDLOAD - DATA
      SET NOT FOUND")], [The staging library is not there: an earlier accept job
      scratched it. #cmd("GUARD") ended with RC 0 and no step ran, so nothing
      changed (@ug-install-accept). Check with #cmd("LIST CDS SYSMOD(TFTP134)")
      that #cmd("REC APP ACC") is there.],
    [The accept job ends with #cmd("COND CODE 0008") in #cmd("NOTAPPL")],
      [#cmd("GUARD") did not find #cmd("TFTP134") in the target zone (it ended
      with RC 4): the install job never ran, ended with RC 8 or 12 before the
      #cmd("APPLY"), or the function was taken out again. #cmd("ACCEPT") and
      #cmd("CLEANUP") did not run\; nothing is accepted or scratched. Measured
      after a removal (@ug-install-accept).],
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
    [#cmd("FTPD004E THE SECURITY SYSTEM ACCEPTS ANY USERID AND PASSWORD"), and
      every #cmd("PASS") gets #cmd("530 Login not possible: the security check of this server is not working ...")], [The security tables do not work
      as FTPD needs them (@ug-security-check). Nobody can log in. Correct the
      tables (#cmd("docs/FTPD_RAKF_SETUP.md")), then stop and start FTPD.],
    [#cmd("FTPD002E FTPD IS ALREADY ACTIVE")], [An older instance is up.
      #cmd("P FTPD") first.],
    [#cmd("530") for a valid user], [The RAKF user or password is wrong, or the
      profile #cmd("FTPAUTH") denies the user (@ug-security).],
    [#cmd("451") on every JES command], [A #cmd("HASPCKPT") or #cmd("HASPACE1")
      DD in the procedure names the wrong data set or volume
      (@ug-install-jes).],
    [#cmd("550 UFS service not available")], [The UFSD started task is not
      running. Installed is not enough (@ug-install-ufsd).],
    [Every transfer waits about 30 seconds with some clients, and not with
      others], [The host is behind NAT, port forwarding or a container
      network, and #cmd("PASVADR") is still #cmd("ANY"). FTPD then puts the
      local address of the control connection into the #cmd("227") reply, and
      that address is private. A client that connects to the address in
      #cmd("227") waits for its time-out, then tries the address of the control
      connection. The Zowe FTP plugin 3.1.1 does so, with a time-out of 30
      seconds. curl and Python #cmd("ftplib") connect to the control address
      at once and see no delay. Set #cmd("PASVADR") to the address that the
      client can reach, forward the whole #cmd("PASVPORTS") range to the host,
      and start FTPD again (@ug-ops-config).],
  )
]
