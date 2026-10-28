#import "../bookmaster/bookmaster.typ": *

= Removing FTPD <ug-remove>

#idx("removal")
#idx("uninstall")
This chapter takes FTPD off an MVS 3.8j system and frees its FMID, so that
it can be installed again. The main way lets SMP do the work: a dummy
function, #cmd("TFTPDEL"), deletes every release of FTPD, and the load module
leaves both libraries with it. A way that uses UCLIN alone stays as the
fallback (@ug-remove-uclin). If you want to move to a newer release, you are
in the wrong chapter: a release deletes its predecessor when it is installed
(@ug-install-upgrade), and the last step below would scratch the libraries
that the new release installs into.

#note[*Did you install a development build of #cmd("main")?* Its FMID is
#cmd("TFTP140"), which none of the steps below names. Start with
@ug-remove-dev.]

#note[*The removal instructions in older archives are wrong.* The file
#cmd("README.md") in #cmd("ftpd-1.0.?-dist.zip") says to run #cmd("RESTORE")
and then #cmd("REJECT"). Both are refused once the FMID has been accepted,
which the install job does in the same run as the #cmd("APPLY")
(@ug-remove-why). From 1.1.0 the shipped README says so itself. The UCLIN job
of the installation guide as it stood up to 1.2.0 had a second flaw: it ran
both zones in one step, and on a system that never carried #cmd("TFTP100") the
step ended early and left the ACDS untouched (@ug-remove-uclin). Use this
chapter instead.]

== What FTPD Put on the System <ug-remove-what>

#tab(caption: [What release 1.3.2 installed])[
  #table(columns: (1.6in, 1fr),
    [Item], [Name],
    [FMID], [#cmd("TFTP132")],
    [Load module], [#cmd("FTPD")],
    [Target library], [#cmd("FTPD.LINKLIB")],
    [Distribution library], [#cmd("FTPD.AFTPDLOD")],
    [Sample library], [#cmd("FTPD.SAMPLIB")],
  )
]

The data set names carry no version, so there is only one FTPD installation
on a system, whichever release put it there. Before 1.1.0 the names carried the
patch level (#cmd("FTPD.V1R0M2.LINKLIB")) while the FMID moved only per minor.
If you find data sets of this kind, add the qualifier to every name below. The
jobs below have not been measured on such a system.

The staging library #cmd("FTPD.FTPDLOAD") is not on the list, because the
cleanup step of the install job scratched it.

*Earlier releases left their FMIDs behind.* Since 1.1.0 there is one FMID for
each release: #cmd("TFTP110") is 1.1.0 and nothing else, #cmd("TFTP120") is
1.2.0, #cmd("TFTP130") is 1.3.0, #cmd("TFTP131") is 1.3.1 and #cmd("TFTP132")
is 1.3.2. #cmd("TFTP100") is the exception, from the
policy that came before it: it covered 1.0.0, 1.0.1 and 1.0.2 alike. Each
upgrade deletes the release before it and leaves its id in the inventory as a
mark, in both zones:

#screen(```
TFTP131   TYPE            = FUNCTION
          DELBY           = TFTP132
```)

A system that went from 1.0.x through 1.1.0, 1.2.0, 1.3.0 and 1.3.1 to 1.3.2
carries one such mark for every step. Deleting the id of the current release and of its
predecessor clears only the newest of them. Both ways below therefore name
every id that FTPD has ever used (#cmd("TFTP100"), #cmd("TFTP110"),
#cmd("TFTP120"), #cmd("TFTP130"), #cmd("TFTP131") and #cmd("TFTP132")), and you
need not know which releases the system has seen. The jobs are written for
releases up to 1.3.2. A later
release adds its own id to every list in this chapter.

== Step 1: Stop the Server <ug-remove-stop>

#screen(```
P FTPD
```)

== Step 2: Delete the Release with the Dummy Function <ug-remove-dummy>

#idx("TFTPDEL")
#idx("dummy function")
SMP deletes a function when a later function names it in a #cmd("DELETE")
operand, and it removes the load module of the deleted function from the
libraries on the way. The job below receives, applies and accepts such a
function. #cmd("TFTPDEL") has no elements. It deletes all FTPD ids up to 1.3.2
in one #cmd("DELETE") statement.

*#cmd("TFTPDEL") is not a release FMID.* No release of FTPD ships it, the
job brings it in by itself, and step 3 frees it again in the same flow. Do not
use it for anything else, and do not keep it on the system.

Two things in the job matter:

- The #cmd("APPLY") and the #cmd("ACCEPT") step each carry a
  #cmd("HMASMP.LINKLIB") and a #cmd("HMASMP.AFTPDLOD") statement. SMP deletes
  the module from the libraries that these DD names point to. Without
  #cmd("HMASMP.AFTPDLOD"), the #cmd("ACCEPT") ends with return code 12 and the
  message #cmd("HMA2833 ** AFTPDLOD DDCARD MISSING"). Without
  #cmd("HMASMP.LINKLIB"), the #cmd("APPLY") still ends with return code 04 and
  prints the same #cmd("HMA2240") line, but it deletes from the library that
  the procedure names itself (#cmd("SYS1.LINKLIB") on the system measured),
  and #cmd("FTPD.LINKLIB") keeps its member. The member list of the last step
  is the only proof. The statements come after the #cmd("EXEC") statement and
  before #cmd("SMPCNTL"), as in the install job.
- The later steps run only on #cmd("COND=(4,LT,...)"). The #cmd("RECEIVE") ends
  with return code 04, because a function without elements raises
  #cmd("HMA3971"). With the usual #cmd("COND=(0,NE,...)") the job would stop
  right after the receive.

Submit this job:

#screen(```
//FTPDDEL  JOB (SYS),'FTPD DELETE',
//             CLASS=A,MSGCLASS=H,MSGLEVEL=(1,1),
//             REGION=4096K
//RECV    EXEC SMPREC
//SMPPTFIN DD  *
++FUNCTION(TFTPDEL) .
++VER(Z038) DELETE(TFTP100,TFTP110,TFTP120,TFTP130,TFTP131,TFTP132) .
/*
//SMPCNTL  DD  *
 RESETRC .
 RECEIVE SELECT(TFTPDEL) .
/*
//APPLY   EXEC SMPAPP,COND=(4,LT,RECV.HMASMP)
//HMASMP.LINKLIB  DD  DISP=SHR,DSN=FTPD.LINKLIB
//HMASMP.AFTPDLOD DD  DISP=SHR,DSN=FTPD.AFTPDLOD
//SMPCNTL  DD  *
 APPLY S(TFTPDEL) DIS(WRITE) .
/*
//ACCEPT  EXEC SMPAPP,COND=(4,LT,APPLY.HMASMP)
//HMASMP.LINKLIB  DD  DISP=SHR,DSN=FTPD.LINKLIB
//HMASMP.AFTPDLOD DD  DISP=SHR,DSN=FTPD.AFTPDLOD
//SMPCNTL  DD  *
 ACCEPT S(TFTPDEL) DIS(WRITE) .
/*
//MEMBERS EXEC PGM=IKJEFT01
//SYSTSPRT DD  SYSOUT=*
//SYSTSIN  DD  *
 LISTDS 'FTPD.LINKLIB' MEMBERS
 LISTDS 'FTPD.AFTPDLOD' MEMBERS
/*
//
```)

Read the three SMP steps one by one. The receive ends with return code 04:

#screen(raw(read("../ex/ug-remove/dummy-recv.txt")))

The apply deletes the load module from the target library. It also ends with
return code 04, because SMP does not find the release that you remove in the
restore data set (#cmd("HMA2461")). The output below comes from a system with
release 1.3.2, so the message names #cmd("TFTP132"), the release that you
remove:

#screen(raw(read("../ex/ug-remove/dummy-apply.txt")))

The accept deletes the copy from the distribution library and ends with return
code 00:

#screen(raw(read("../ex/ug-remove/dummy-accept.txt")))

*The result is in the last step, not in the return codes.* Both libraries
must list no member. Everything else is the proof that the job ran, not that
the work happened:

#screen(raw(read("../ex/ug-remove/members-empty.txt")))

If a step ends higher than 04, the following #cmd("APPLY") or #cmd("ACCEPT")
step does not run; the last step does, and shows what is still there. Read
the output of the failed step; do not repeat the job before you know what it
did.

*What has been measured.* The job was run on a test system with a new
SMP installation of release 1.3.2; the screens of this chapter are from that
run. It was run a second time on a system that carried 1.2.0 through SMP and
had received the install job of 1.3.2 on top of it. That job copies nothing
(@ug-install-upgrade), so #cmd("TFTP120") still owned the module. The return
codes were the same, and both libraries were empty afterwards. The only
difference is in the #cmd("APPLY"): it printed #cmd("HMA2461") twice, once for
#cmd("TFTP120") and once for #cmd("TFTP132"). A system whose data sets carry
the patch level in their names, and a dummy function with a #cmd("++MAC")
element instead of none, have not been measured. Whatever the system, the
#cmd("LIST") in step 4 is the proof.

== Step 3: Free the Ids <ug-remove-free>

#idx("UCLIN")
#idx("FMID", "removal")
The dummy function has not made the ids free. It has left #cmd("TFTPDEL")
itself in both zones, with the status #cmd("REC APP ACC"), and a mark
#cmd("DELBY = TFTPDEL") on every id it deleted. This job removes the seven
entries from the CDS (what is applied) and the ACDS (what is accepted). It
changes the two SMP data bases and no library:

#screen(```
//FTPDUCL  JOB (SYS),'FTPD UNINSTALL',
//             CLASS=A,MSGCLASS=H,MSGLEVEL=(1,1),
//             REGION=4096K
//UCLCDS  EXEC SMPAPP
//SMPCNTL  DD  *
 UCLIN CDS .
  DEL SYSMOD(TFTPDEL) .
  DEL SYSMOD(TFTP132) .
  DEL SYSMOD(TFTP131) .
  DEL SYSMOD(TFTP130) .
  DEL SYSMOD(TFTP120) .
  DEL SYSMOD(TFTP110) .
  DEL SYSMOD(TFTP100) .
 ENDUCL .
/*
//UCLACDS EXEC SMPAPP
//SMPCNTL  DD  *
 UCLIN ACDS .
  DEL SYSMOD(TFTPDEL) .
  DEL SYSMOD(TFTP132) .
  DEL SYSMOD(TFTP131) .
  DEL SYSMOD(TFTP130) .
  DEL SYSMOD(TFTP120) .
  DEL SYSMOD(TFTP110) .
  DEL SYSMOD(TFTP100) .
 ENDUCL .
/*
//LIST    EXEC SMPAPP
//SMPCNTL  DD  *
 RESETRC .
 LIST CDS  SYSMOD(TFTPDEL) .
 LIST ACDS SYSMOD(TFTPDEL) .
 LIST CDS  SYSMOD(TFTP132) .
 LIST ACDS SYSMOD(TFTP132) .
 LIST CDS  SYSMOD(TFTP131) .
 LIST ACDS SYSMOD(TFTP131) .
 LIST CDS  SYSMOD(TFTP130) .
 LIST ACDS SYSMOD(TFTP130) .
 LIST CDS  SYSMOD(TFTP120) .
 LIST ACDS SYSMOD(TFTP120) .
 LIST CDS  SYSMOD(TFTP110) .
 LIST ACDS SYSMOD(TFTP110) .
 LIST CDS  SYSMOD(TFTP100) .
 LIST ACDS SYSMOD(TFTP100) .
/*
//
```)

Every #cmd("DEL") finds its entry, because the dummy function left a mark for
every id it named, also for those that the system never had. Both #cmd("UCLIN")
steps end with return code 00, and each #cmd("DEL") reports
#cmd("HMA2550 UPDATE COMPLETE"). This is the output of the step
#cmd("UCLCDS"):

#screen(raw(read("../ex/ug-remove/uclcds-free.txt")))

The step #cmd("UCLACDS") gives the same. Any other message is not expected.

*This job depends on step 2.* For an id that is not in the zone,
#cmd("DEL SYSMOD") ends the step with return code 08 (@ug-remove-uclin). Only
the marks of the dummy function make all seven ids exist, so run this job after
step 2 and not instead of it.

== Step 4: Read the LIST <ug-remove-list>

The last step of the job is the result. For every id, in both zones, it must
answer that the entry was not found. These are the first two of the fourteen
statements, as the step printed them:

#screen(raw(read("../ex/ug-remove/list-gone.txt")))

The step must end with the highest return code 04. Return code 04 together
with this answer means that the FMID is free, and #cmd("TFTPDEL") with it.
Both zones count: an id gone from one and still in the other is not free.

*Read the stanzas, not the return code of the step.* A mark that is still
there answers with return code 00, so it does not raise the return code of
the step above the 04 that you expect. The job has fourteen #cmd("LIST")
statements, and all fourteen must show the id as not found. A stanza that names
#cmd("TYPE = FUNCTION") is an entry that is still there, whether it shows the
status #cmd("REC APP ACC") or only a #cmd("DELBY") of its successor. This is
what the statements #cmd("LIST CDS SYSMOD(TFTP132)") and
#cmd("LIST CDS SYSMOD(TFTPDEL)") print after step 2, before step 3 has run:

#screen(raw(read("../ex/ug-remove/list-present.txt")))

== Step 5: Scratch the Libraries <ug-remove-scratch>

After step 2 the two libraries are empty, but they still exist. The allocation
job of the installation creates both with #cmd("DISP=(NEW,CATLG,DELETE)") and
is meant for names that are free. Delete them in a job of your own:

#screen(```
//FTPDSCR  JOB (SYS),'FTPD SCRATCH',
//             CLASS=A,MSGCLASS=H,MSGLEVEL=(1,1),
//             REGION=4096K
//IDCAMS  EXEC PGM=IDCAMS
//SYSPRINT DD  SYSOUT=*
//SYSIN    DD  *
  DELETE FTPD.LINKLIB  NONVSAM SCRATCH PURGE
  DELETE FTPD.AFTPDLOD NONVSAM SCRATCH PURGE
/*
//
```)

Each statement ends with #cmd("IDC0550I ENTRY (A) ... DELETED").

*Do this only when you remove FTPD for good.* After an upgrade, these are the
data sets that the next release installs into; leave them. #cmd("FTPD.SAMPLIB")
may stay. The install job scratches it itself the next time.

== The Fallback: UCLIN Alone <ug-remove-uclin>

#idx("UCLIN")
Use this way when the dummy function cannot be used. After step 1, submit this
job instead of steps 2 and 3. It changes the two SMP data bases and no
library. In the first line of each block, name the FMID that the system
carries (#cmd("LIST CDS MOD(FTPD) .") shows it as #cmd("FMID")); the job names
#cmd("TFTP132"):

#screen(```
//FTPDUCL  JOB (SYS),'FTPD UNINSTALL',
//             CLASS=A,MSGCLASS=H,MSGLEVEL=(1,1),
//             REGION=4096K
//UCLCDS  EXEC SMPAPP
//SMPCNTL  DD  *
 UCLIN CDS .
  DEL SYSMOD(TFTP132) MOD(FTPD) .
  DEL MOD(FTPD) .
  DEL LMOD(FTPD) .
  DEL SYSMOD(TFTP132) .
  DEL SYSMOD(TFTP131) .
  DEL SYSMOD(TFTP130) .
  DEL SYSMOD(TFTP120) .
  DEL SYSMOD(TFTP110) .
  DEL SYSMOD(TFTP100) .
 ENDUCL .
/*
//UCLACDS EXEC SMPAPP
//SMPCNTL  DD  *
 UCLIN ACDS .
  DEL SYSMOD(TFTP132) MOD(FTPD) .
  DEL MOD(FTPD) .
  DEL SYSMOD(TFTP132) .
  DEL SYSMOD(TFTP131) .
  DEL SYSMOD(TFTP130) .
  DEL SYSMOD(TFTP120) .
  DEL SYSMOD(TFTP110) .
  DEL SYSMOD(TFTP100) .
 ENDUCL .
/*
//LIST    EXEC SMPAPP
//SMPCNTL  DD  *
 RESETRC .
 LIST CDS  SYSMOD(TFTP132) .
 LIST ACDS SYSMOD(TFTP132) .
 LIST CDS  SYSMOD(TFTP131) .
 LIST ACDS SYSMOD(TFTP131) .
 LIST CDS  SYSMOD(TFTP130) .
 LIST ACDS SYSMOD(TFTP130) .
 LIST CDS  SYSMOD(TFTP120) .
 LIST ACDS SYSMOD(TFTP120) .
 LIST CDS  SYSMOD(TFTP110) .
 LIST ACDS SYSMOD(TFTP110) .
 LIST CDS  SYSMOD(TFTP100) .
 LIST ACDS SYSMOD(TFTP100) .
 LIST CDS  MOD(FTPD) LMOD(FTPD) .
 LIST ACDS MOD(FTPD) .
/*
//
```)

Every #cmd("DEL") that finds its entry reports #cmd("HMA2550 UPDATE COMPLETE").
An id that was never in the zone is reported as #cmd("HMA2532") and
#cmd("HMA2560"), and the step ends with return code 08. That is expected for
the ids your system never had; the statements after it are still processed. It
is no sign that a line did what you wanted, and a step that ends with return
code 08 can also hide a statement that failed for another reason. Read every
message, and take the #cmd("LIST") as the only proof. This is the output of the
step #cmd("UCLCDS") on a system with a new installation of release 1.3.2.
#cmd("TFTP131") is in the zone as the mark that the install left;
#cmd("TFTP130"), #cmd("TFTP120"), #cmd("TFTP110") and #cmd("TFTP100") were
never in it:

#screen(raw(read("../ex/ug-remove/uclcds.txt")))

The step #cmd("UCLACDS") gives the same, without the #cmd("LMOD") statement.

*The two zones have a step each for a reason.* After a step has ended with
return code 08, SMP does not execute the next #cmd("UCLIN") of the same
step: it answers #cmd("HMA2293 ** CONTROL STATEMENT IGNORED DUE TO PREVIOUS
ERROR") and does nothing. With both blocks in one step, the ids that the
system never had would leave the ACDS untouched. Measured on a system with
a new installation of 1.3.2, with both blocks of the job above in one step:
the #cmd("UCLIN CDS") block ended with return code 08 at
#cmd("DEL SYSMOD(TFTP130)"), the #cmd("UCLIN ACDS") block that followed was
ignored, the step ended with return code 12, and
#cmd("LIST ACDS SYSMOD(TFTP132)") still showed the entry.

Then read the #cmd("LIST") as in step 4, here fourteen statements without
#cmd("TFTPDEL") and with the two module entries, and, if you remove FTPD for
good, scratch the libraries as in step 5. #cmd("UCLIN") changes only the
inventory: until you scratch them, the load module is still in the target
library, and the copy that SMP accepted is in the distribution library.

== You Installed a Development Build <ug-remove-dev>

#idx("development build")
#idx("TFTP140")
A development build of #cmd("main") is packaged under the FMID of the release
that #cmd("main") is heading for. A build of 1.4.0-dev carries
#cmd("TFTP140"), although 1.4.0 is not released. If you installed such a
package by SMP, the system holds an id that no step above names. Neither the
dummy function of step 2 nor the lists of step 3 know it, so it stays in both
zones, and it still owns the load module. Install a release only after you
have cleaned the system up as described here.

Ask SMP which id the system carries, with this job under the SMP procedure of
your system (it is the one of @ug-install-upgrade):

#screen(```
//LIST    EXEC SMPAPP
//SMPCNTL  DD  *
 LIST CDS  MOD(FTPD) .
/*
```)

#cmd("FMID = TFTP140") means that you are in the right section, and so does
any other id except #cmd("TFTP100"), #cmd("TFTP110"), #cmd("TFTP120"),
#cmd("TFTP130"), #cmd("TFTP131") and #cmd("TFTP132"). Only #cmd("TFTP140") has
been measured. If the #cmd("FMID") is one of those six, the system holds a
release, the main way removes it, and this section is not yours.

The steps are those of the main way, with three differences: the release package is on the system before FTPD stops,
#cmd("TFTP140") is named in every list, and the libraries stay.

=== Upload the Release Package First <ug-remove-dev-upload>

FTPD is the way the package gets onto the system, and you stop it in the next
step. Upload both #cmd(".xmit") files of the release now, in binary, as
described in @ug-install-upload, and edit the install job of the package for
your data set names. Do not run it yet.

=== Stop the Server, Then Delete the Development Build <ug-remove-dev-dummy>

Stop FTPD with #cmd("/P FTPD"). Then submit the job of step 2 with one change:
#cmd("TFTP140") is added to the #cmd("DELETE") operand. Everything else stays
as it is there: the #cmd("HMASMP.LINKLIB") and #cmd("HMASMP.AFTPDLOD")
statements, the #cmd("COND") on the later steps and the member lists at the
end.

*Continue the #cmd("++VER") card on a second line.* With #cmd("TFTP140") the
card is 77 characters long. SMP rejects it: the #cmd("RECEIVE") ends with
return code 08 and prints #cmd("HMA2032 ** SYNTAX ERROR IN CONTROL STATEMENT AT COL 68"),
which is the start of the last id, and the job ends with 12. The job below
breaks the card after #cmd("TFTP132,") and continues it on the next line, which
SMP accepts. Do not type the card on one line.

#screen(```
//FTPDDEL  JOB (SYS),'FTPD DELETE',
//             CLASS=A,MSGCLASS=H,MSGLEVEL=(1,1),
//             REGION=4096K
//RECV    EXEC SMPREC
//SMPPTFIN DD  *
++FUNCTION(TFTPDEL) .
++VER(Z038) DELETE(TFTP100,TFTP110,TFTP120,TFTP130,TFTP131,TFTP132,
            TFTP140) .
/*
//SMPCNTL  DD  *
 RESETRC .
 RECEIVE SELECT(TFTPDEL) .
/*
//APPLY   EXEC SMPAPP,COND=(4,LT,RECV.HMASMP)
//HMASMP.LINKLIB  DD  DISP=SHR,DSN=FTPD.LINKLIB
//HMASMP.AFTPDLOD DD  DISP=SHR,DSN=FTPD.AFTPDLOD
//SMPCNTL  DD  *
 APPLY S(TFTPDEL) DIS(WRITE) .
/*
//ACCEPT  EXEC SMPAPP,COND=(4,LT,APPLY.HMASMP)
//HMASMP.LINKLIB  DD  DISP=SHR,DSN=FTPD.LINKLIB
//HMASMP.AFTPDLOD DD  DISP=SHR,DSN=FTPD.AFTPDLOD
//SMPCNTL  DD  *
 ACCEPT S(TFTPDEL) DIS(WRITE) .
/*
//MEMBERS EXEC PGM=IKJEFT01
//SYSTSPRT DD  SYSOUT=*
//SYSTSIN  DD  *
 LISTDS 'FTPD.LINKLIB' MEMBERS
 LISTDS 'FTPD.AFTPDLOD' MEMBERS
/*
//
```)

The receive and the apply end with return code 04, the accept with 00, as in
step 2. The #cmd("APPLY") prints exactly one #cmd("HMA2461"), and it names the
development id:

#screen(raw(read("../ex/ug-remove/dev-recv.txt")))

#screen(raw(read("../ex/ug-remove/dev-apply.txt")))

#screen(raw(read("../ex/ug-remove/dev-accept.txt")))

The member lists are empty, as in step 2. Read them as there: both libraries
must list no member.

*Check this step before you go on.* If the #cmd("RECEIVE") ended with 08, as
it does with the one-line card, the job removed nothing: the member lists of its
last step still name #cmd("FTPD") in both libraries. Do not run the next step
then, because #cmd("UCLIN") changes only the inventory and leaves the load
module where it is. Fix the card and run this step again. Go on when the
#cmd("APPLY") and the #cmd("ACCEPT") have printed their #cmd("HMA2240") lines
and both member lists are empty.

=== Free the Ids, With TFTP140 in the Lists <ug-remove-dev-free>

Submit the job of step 3 with #cmd("TFTP140") added to the #cmd("DEL")
statements of both #cmd("UCLIN") blocks and to the #cmd("LIST") statements.
#cmd("TFTP140") goes right after #cmd("TFTPDEL") and ahead of #cmd("TFTP132"),
in the #cmd("DEL") blocks and in the #cmd("LIST") statements alike:

#screen(```
//FTPDUCL  JOB (SYS),'FTPD UNINSTALL',
//             CLASS=A,MSGCLASS=H,MSGLEVEL=(1,1),
//             REGION=4096K
//UCLCDS  EXEC SMPAPP
//SMPCNTL  DD  *
 UCLIN CDS .
  DEL SYSMOD(TFTPDEL) .
  DEL SYSMOD(TFTP140) .
  DEL SYSMOD(TFTP132) .
  DEL SYSMOD(TFTP131) .
  DEL SYSMOD(TFTP130) .
  DEL SYSMOD(TFTP120) .
  DEL SYSMOD(TFTP110) .
  DEL SYSMOD(TFTP100) .
 ENDUCL .
/*
//UCLACDS EXEC SMPAPP
//SMPCNTL  DD  *
 UCLIN ACDS .
  DEL SYSMOD(TFTPDEL) .
  DEL SYSMOD(TFTP140) .
  DEL SYSMOD(TFTP132) .
  DEL SYSMOD(TFTP131) .
  DEL SYSMOD(TFTP130) .
  DEL SYSMOD(TFTP120) .
  DEL SYSMOD(TFTP110) .
  DEL SYSMOD(TFTP100) .
 ENDUCL .
/*
//LIST    EXEC SMPAPP
//SMPCNTL  DD  *
 RESETRC .
 LIST CDS  SYSMOD(TFTPDEL) .
 LIST ACDS SYSMOD(TFTPDEL) .
 LIST CDS  SYSMOD(TFTP140) .
 LIST ACDS SYSMOD(TFTP140) .
 LIST CDS  SYSMOD(TFTP132) .
 LIST ACDS SYSMOD(TFTP132) .
 LIST CDS  SYSMOD(TFTP131) .
 LIST ACDS SYSMOD(TFTP131) .
 LIST CDS  SYSMOD(TFTP130) .
 LIST ACDS SYSMOD(TFTP130) .
 LIST CDS  SYSMOD(TFTP120) .
 LIST ACDS SYSMOD(TFTP120) .
 LIST CDS  SYSMOD(TFTP110) .
 LIST ACDS SYSMOD(TFTP110) .
 LIST CDS  SYSMOD(TFTP100) .
 LIST ACDS SYSMOD(TFTP100) .
/*
//
```)

Both #cmd("UCLIN") steps end with return code 00. Each zone reports eight
#cmd("HMA2550 UPDATE COMPLETE"), one per id, #cmd("TFTP140") among them. This
is the step #cmd("UCLCDS"):

#screen(raw(read("../ex/ug-remove/dev-uclcds.txt")))

The step #cmd("UCLACDS") prints the same lines. On the test systems this job
was enough: no #cmd("HMA2532") or #cmd("HMA2560") appeared in either zone, and
the ACDS needed no statement beyond these.

Read the #cmd("LIST") as in step 4: sixteen statements, and every one must show
its id as not found. On the test systems every statement did, and the step ended
with return code 04. A stanza with #cmd("TYPE = FUNCTION") for #cmd("TFTP140")
is an entry that is still there, in whichever zone it shows.

If you install a release next, do not scratch the libraries (step 5 of the
main way). The release installs into #cmd("FTPD.LINKLIB") and
#cmd("FTPD.AFTPDLOD"), which are empty now.

*If you want FTPD gone and install no release,* you need no release package:
leave out the first step of this section, and scratch the libraries as in
@ug-remove-scratch after the step above. Then continue with @ug-remove-left.
The steps from stopping the server to freeing the ids have been run, and step 5
of the main way is the one that scratches the libraries. The combination of the
two has not been run.

=== Install the Release and Check the Result <ug-remove-dev-install>

Run the install job of the release package that you prepared in the first
step. *Do not run the allocation job.* It allocates both libraries #cmd("NEW"),
and the libraries exist. This has not been run: the test runs skipped the
allocation job after the development install. The steps and the checks are those of the
upgrade (@ug-install-upgrade).

#cmd("RECEIVE"), #cmd("APPLY CHECK"), #cmd("APPLY") and #cmd("ACCEPT") all end
with return code 00. The #cmd("APPLY") prints no #cmd("HMA2461"). Both steps
copy the module, and that is the line to look for:

#screen(raw(read("../ex/ug-remove/dev-apply-rc.txt")))

Then list the members of #cmd("FTPD.LINKLIB") and #cmd("FTPD.AFTPDLOD"):
#cmd("FTPD") must be in both.

#screen(raw(read("../ex/ug-remove/dev-members.txt")))

#cmd("LIST CDS MOD(FTPD) .") and #cmd("LIST ACDS MOD(FTPD) .") must each answer
with the release as #cmd("FMID") and #cmd("RMID"), and not #cmd("TFTP140").
This is the answer of the CDS:

#screen(raw(read("../ex/ug-remove/dev-listmod.txt")))

Start FTPD and read the banner. It must name the release (the line
#cmd("FTPD062W") depends on your parameter file):

#screen(raw(read("../ex/ug-remove/dev-banner.txt")))

*A banner that says #cmd("1.4.0-DEV") after this means that the server was
loaded from another library.* #cmd("mbt deploy") writes a development build to
#cmd("FTPD.DEV.LINKLIB") by default, and a procedure whose #cmd("STEPLIB")
names that library ahead of #cmd("FTPD.LINKLIB") loads the module from there.
An SMP #cmd("DELETE") acts only on the library that the
#cmd("HMASMP.LINKLIB") statement of its own job names, and the release install
writes to #cmd("FTPD.LINKLIB"). Neither touches #cmd("FTPD.DEV.LINKLIB"). So
every check above can pass, #cmd("FTPD.LINKLIB") can hold the release, and the
server that starts is still the development build. Read the #cmd("STEPLIB") of
your started task procedure. If #cmd("FTPD.DEV.LINKLIB") is named there, take
it out, or scratch the library if you do not need it any more, and start FTPD
again. The behaviour of SMP was measured on an MVS/CE system with another
product that is built and deployed the same way\; it has not been run with
FTPD.

*What has been measured.* The flow of this section was run on a test system
started from the TK5 image and on one started from the MVS/CE image, each with
its own run for the steps above and for the check that follows. Both carried a
development build of 1.4.0 (#cmd("TFTP140")) that was installed with its own
install job, but they did not hold the same history. On the MVS/CE system there
was no earlier FTPD in the inventory, and the #cmd("APPLY") of the development
build ended with return code 00. On the TK5 system the image held an SMP
installation of 1.2.0 (#cmd("TFTP120")) and the development build was
installed over it: its #cmd("APPLY") ended with return code 04 and one
#cmd("HMA2461 SYSMOD TFTP120 NOT FOUND ON SMPSCDS LIBRARY"). Both systems gave
the same return codes and the same messages in every step of this section, with
one difference in a report that this book does not quote: the status report of
the #cmd("APPLY") in the second step lists #cmd("TFTP110") as
#cmd("PREVIOUSLY DELETED") on the TK5 system and as
#cmd("NOT PREVIOUSLY INSTALLED") on the MVS/CE system.

Where this section speaks of release 1.3.2, the runs used the package of its
release candidate. It carries the same FMID, #cmd("TFTP132"), and the source of
the module did not change between the candidate and the release. The banner
hash is that of the candidate, and the screen shows a placeholder for it.

*The development id keeps the module.* The check was to install the 1.3.2
package over the development build without this section. #cmd("RECEIVE"),
#cmd("APPLY") and #cmd("ACCEPT") all end with return code 00, and the job
prints no #cmd("HMA2380") line: SMP copies nothing. The summary of the
#cmd("APPLY") and of the #cmd("ACCEPT") lists #cmd("MOD FTPD") as
#cmd("NOT SEL"). #cmd("LIST CDS MOD(FTPD)") still names #cmd("TFTP140") as
#cmd("FMID") and #cmd("RMID") in both zones, and the server that you start
announces itself as 1.4.0-DEV. Only the member lists, the #cmd("LIST") and the
banner show this. The return codes do not.

*What has not been measured.* A system with a longer history than the TK5
system, for example one that holds ids from earlier releases that SMP lost
track of. A second development build installed over the first has not been
measured with FTPD either. A test with another product that is built the same
way showed that installing the same development id again ends with return code
0, and that a second, different development id deletes the first with one
#cmd("HMA2461") for it. If your development build carries another id than
#cmd("TFTP140"), put that id in the #cmd("DELETE") operand of the dummy
function and in the lists of the step that frees the ids.

== What Is Not Removed <ug-remove-left>

SMP never owned these, so you have to take care of them:

- the procedure and the member of the configuration that you copied into the
  procedure library and the parameter library of the system
  (@ug-install-proc);
- the entry for #cmd("FTPD.LINKLIB") in the APF list, if you made one;
- #cmd("FTPD.DEV.LINKLIB"), the library that #cmd("mbt deploy") writes a
  development build to, and its entry in the APF list, if you made one. SMP
  does not own it. Neither the steps above nor the install of a release clears
  it (@ug-remove-dev-install);
- your RAKF definitions: the #cmd("FTPAUTH") profile, and any identity that you
  mapped the started task #cmd("FTPD") to (@ug-security-stc). Releases before
  1.3.0 switched to an #cmd("FTPD") user at startup.

== Why RESTORE and REJECT Do Not Work <ug-remove-why>

The install job accepts the FMID in the same run in which it applies it. That
closes both of the usual ways back:

#screen(raw(read("../ex/ug-remove/restore-reject.txt")))

#cmd("RESTORE") refuses a SYSMOD that has been accepted
(#cmd("HMA2452"), return code 12). #cmd("REJECT") then fails for a reason that
looks unrelated: #cmd("HMA2462") says that the SYSMOD is not on the
#cmd("SMPPTS") data set, although the job received it from there (return code
12).

The cause, on the system where this was measured, is the option
#cmd("PURGE=YES") in the system entry of the #cmd("SMPPTS"). With it, SMP
deletes a SYSMOD from the PTS as soon as it has accepted it. The entry shows
with #cmd("LIST PTS ."):

#screen(raw(read("../ex/ug-remove/pts-system.txt")))

Two things have not been measured: a system with #cmd("PURGE=NO"), and a
#cmd("REJECT") after the #cmd("ACCEPT") there. No #cmd("UCLIN") statement was
found that changes the system entry, so this book does not change it. It
removes the FMID in ways that do not need the PTS.

The price of the accept is that the FMID itself becomes permanent by
documented means. What the accept buys is that a later #cmd("RESTORE") of a PTF
returns to the distribution library; without it, a #cmd("RESTORE") would delete
the module rather than revert it, because there would be no previous level to
go back to.
