# Removing FTPD

This is the supported way to take FTPD back off an MVS 3.8j system and free
its FMID for a re-install.

The main way lets SMP do the work: a dummy function, `TFTPDEL`, deletes every
release of FTPD, and the load module leaves both libraries with it. A way that
uses `UCLIN` alone stays as the [fallback](#the-fallback-uclin-alone).

> **The removal instructions in older archives are wrong.**
> `README.md` in `ftpd-1.0.?-dist.zip` says to run `RESTORE` and then
> `REJECT`. Both are refused once the FMID has been accepted, which the install
> job does in the same run as the APPLY. From 1.1.0 the shipped README says so
> itself and points here. The `UCLIN` job in this document as it stood up to
> 1.2.0 has a second flaw: it ran both zones in one step, and on a system that
> never carried `TFTP100` the step ended early and left the ACDS untouched (see
> [the fallback](#the-fallback-uclin-alone)). Use this document instead.

> **Did you install a development build of `main`?** Its FMID is `TFTP140`, which
> none of the jobs below names. Go to
> [the section on development builds](#you-installed-a-development-build-with-a-later-fmid)
> first.

> **If you are upgrading rather than removing, you are in the wrong document.**
> A release deletes its predecessor as part of its own install, and the
> scratch step below would delete the libraries the new release installs into.
> See the upgrade sections of [installation.md](installation.md).

## What this release put on the system

| | |
|---|---|
| FMID | `TFTP132` |
| Load module | `FTPD` |
| Target library | `FTPD.LINKLIB` |
| Distribution library | `FTPD.AFTPDLOD` |
| Sample library | `FTPD.SAMPLIB` |

The data set names carry no version, so there is only one FTPD installation on
a system, whichever release put it there. Before 1.1.0 the names carried the
patch level (`FTPD.V1R0M2.LINKLIB`) while the FMID moved only per minor. If you
find data sets of this kind, add the qualifier to every name below. The jobs
below have not been measured on such a system.

The staging library `FTPD.FTPDLOAD` is not on the list, because the cleanup
step of the install job scratched it.

**Earlier releases left their FMIDs behind.** Since 1.1.0 there is one FMID per
*release*: `TFTP110` is 1.1.0 and nothing else, `TFTP120` is 1.2.0,
`TFTP130` is 1.3.0, `TFTP131` is 1.3.1 and `TFTP132` is 1.3.2. `TFTP100` is the exception, from the
policy that came before it: it covered 1.0.0, 1.0.1 and 1.0.2 alike. Each upgrade deletes the
release before it and leaves its id in the inventory as a mark, in both zones:

```
TFTP130   TYPE            = FUNCTION
          DELBY           = TFTP131
```

A system that went from 1.0.x through 1.1.0, 1.2.0, 1.3.0 and 1.3.1 to 1.3.2 carries
one such mark for every step. Deleting the id of the current release and of its
predecessor clears only the newest of them. Both ways below therefore name
**every id FTPD has ever used** (`TFTP100`, `TFTP110`, `TFTP120`,
`TFTP130`, `TFTP131` and `TFTP132`), and you need not know which releases the
system has seen. The jobs are written for releases up to 1.3.2. If a later release adds an
id, it is added to every list here.

---

## 1. Stop the server

```
/P FTPD
```

## 2. Delete the release with the dummy function

SMP deletes a function when a later function names it in a `DELETE` operand,
and it removes the load module of the deleted function from the libraries on
the way. The job below receives, applies and accepts such a function.
`TFTPDEL` has no elements. It deletes all FTPD ids up to 1.3.2 in one `DELETE`
statement.

**`TFTPDEL` is not a release FMID.** No release of FTPD ships it, the job
brings it in by itself, and step 3 frees it again in the same flow. Do not use
it for anything else, and do not keep it on the system.

Two things in the job matter:

- The `APPLY` and the `ACCEPT` step each carry a `HMASMP.LINKLIB` and a
  `HMASMP.AFTPDLOD` statement. SMP deletes the module from the libraries that
  these DD names point to. Without `HMASMP.AFTPDLOD`, the `ACCEPT` ends with
  return code 12 and the message `HMA2833 ** AFTPDLOD DDCARD MISSING`. Without
  `HMASMP.LINKLIB`, the `APPLY` still ends with return code 04 and prints the
  same `HMA2240` line, but it deletes from the library that the procedure names
  itself (`SYS1.LINKLIB` on the system measured), and `FTPD.LINKLIB` keeps its
  member. The member list of the last step is the only proof. The statements
  come after the `EXEC` statement and before `SMPCNTL`, as in the install job.
- The later steps run only on `COND=(4,LT,...)`. The `RECEIVE` ends with return
  code 04, because a function without elements raises `HMA3971`. With the usual
  `COND=(0,NE,...)` the job would stop right after the receive.

Submit this job:

```
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
```

Read the three SMP steps one by one. The receive ends with return code 04:

```
 RECEIVE SELECT(TFTPDEL) .
----------++FUNCTION(TFTPDEL) .
----------++VER(Z038) DELETE(TFTP100,TFTP110,TFTP120,TFTP130,TFTP131,TFTP132) .
HMA3971    SYSMOD TFTPDEL HAS NO ELEMENTS
HMA3930    SYSMOD TFTPDEL SUCCESSFULLY RECEIVED
HMA2050    RECEIVE PROCESSING COMPLETED - HIGHEST RETURN CODE IS 04
```

The apply deletes the load module from the target library. It also ends with
return code 04, because SMP does not find the release that you remove in the
restore data set (`HMA2461`). The output below comes from a system with
release 1.3.2, so the message names `TFTP132`, the release that you remove:

```
 APPLY S(TFTPDEL) DIS(WRITE) .
HMA2240    SUCCESSFULLY DELETED LMOD FTPD ON LINKLIB LIBRARY
HMA2270    APPLY PROCESSING SUCCESSFULLY COMPLETED FOR SYSMOD TFTPDEL
HMA2461    SYSMOD TFTP132 NOT FOUND ON SMPSCDS LIBRARY
HMA2050    APPLY PROCESSING COMPLETED - HIGHEST RETURN CODE IS 04
```

The accept deletes the copy from the distribution library and ends with return
code 00:

```
 ACCEPT S(TFTPDEL) DIS(WRITE) .
HMA2240    SUCCESSFULLY DELETED MODULE FTPD ON AFTPDLOD LIBRARY
HMA2270    ACCEPT PROCESSING SUCCESSFULLY COMPLETED FOR SYSMOD TFTPDEL
HMA2050    ACCEPT PROCESSING COMPLETED - HIGHEST RETURN CODE IS 00
```

**The result is in the last step, not in the return codes.** Both libraries
must list no member. Everything else is the proof that the job ran, not that the
work happened:

```
 LISTDS 'FTPD.LINKLIB' MEMBERS
FTPD.LINKLIB
--RECFM-LRECL-BLKSIZE-DSORG
  U     **    15040   PO
--VOLUMES--
  TSO001
--MEMBERS--
 LISTDS 'FTPD.AFTPDLOD' MEMBERS
FTPD.AFTPDLOD
--RECFM-LRECL-BLKSIZE-DSORG
  U     **    15040   PO
--VOLUMES--
  TSO001
--MEMBERS--
```

If a step ends higher than 04, the following `APPLY` or `ACCEPT` step does not
run; the last step does, and shows what is still there. Read the output of the
failed step; do not repeat the job before you know what it did.

**What has been measured.** The job was run on a test system with a new
SMP installation of release 1.3.2; the output in this document is from that
run. It was run a second time on a system that carried 1.2.0 through SMP and
had received the install job of 1.3.2 on top of it. That job copies nothing
(see [installation.md](installation.md)), so `TFTP120` still owned the module.
The return codes were the same, and both libraries were empty afterwards. The
only difference is in the `APPLY`: it printed `HMA2461` twice, once for
`TFTP120` and once for `TFTP132`. A system whose data sets carry the patch
level in their names, and a dummy function with a `++MAC` element instead of
none, have not been measured. Whatever the system, the `LIST` in step 4 is the
proof.

## 3. Free the ids

The dummy function has not made the ids free. It has left `TFTPDEL` itself in
both zones, with the status `REC APP ACC`, and a mark `DELBY = TFTPDEL` on every
id it deleted. This job removes the seven entries from the CDS (what is applied)
and the ACDS (what is accepted). It changes the two SMP data bases and no
library:

```
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
```

Every `DEL` finds its entry, because the dummy function left a mark for every id
it named, also for those that the system never had. Both `UCLIN` steps end with
return code 00, and each `DEL` reports `HMA2550 UPDATE COMPLETE`. This is the
output of step `UCLCDS`:

```
 UCLIN CDS .
  DEL SYSMOD(TFTPDEL) .
HMA2550    UPDATE COMPLETE FOR TFTPDEL
  DEL SYSMOD(TFTP132) .
HMA2550    UPDATE COMPLETE FOR TFTP132
  DEL SYSMOD(TFTP131) .
HMA2550    UPDATE COMPLETE FOR TFTP131
  DEL SYSMOD(TFTP130) .
HMA2550    UPDATE COMPLETE FOR TFTP130
  DEL SYSMOD(TFTP120) .
HMA2550    UPDATE COMPLETE FOR TFTP120
  DEL SYSMOD(TFTP110) .
HMA2550    UPDATE COMPLETE FOR TFTP110
  DEL SYSMOD(TFTP100) .
HMA2550    UPDATE COMPLETE FOR TFTP100
 ENDUCL .
HMA2050    UCLIN PROCESSING COMPLETED - HIGHEST RETURN CODE IS 00
```

Step `UCLACDS` gives the same. Any other message is not expected.

**This job depends on step 2.** For an id that is not in the zone, `DEL SYSMOD`
ends the step with return code 08 (see the fallback). Only the marks of the
dummy function make all seven ids exist, so run this job after step 2 and not
instead of it.

## 4. Read the LIST — this is the actual result

The last step of the job is the result. For every id, in both zones, it must
answer that the entry was not found. These are the first two of the fourteen
statements, as the step printed them:

```
 RESETRC .
 LIST CDS  SYSMOD(TFTPDEL) .
LIST     SELECT SUMMARY REPORT FOR SMPCDS
THE FOLLOWING SELECTED ENTRIES WERE NOT FOUND OR WERE NOT ELIGIBLE FOR PROCESSING
 TYPE        NAME
 SYSMOD      TFTPDEL
 LIST ACDS SYSMOD(TFTPDEL) .
LIST     SELECT SUMMARY REPORT FOR SMPACDS
THE FOLLOWING SELECTED ENTRIES WERE NOT FOUND OR WERE NOT ELIGIBLE FOR PROCESSING
 TYPE        NAME
 SYSMOD      TFTPDEL
```

The step must end with the highest return code 04. **Return code 04 together
with this answer means that the FMID is free**, and `TFTPDEL` with it. Both
zones count: an id gone from one and still in the other is not free.

**Read the stanzas, not the return code of the step.** A mark that is still
there answers with return code 00, so it does not raise the return code of the
step above the 04 that you expect. The job has fourteen `LIST` statements, and all
fourteen must show the id as not found. A stanza that names `TYPE = FUNCTION` is an
entry that is still there, whether it shows the status `REC APP ACC` or only a
`DELBY` of its successor. This is what the statements
`LIST CDS SYSMOD(TFTP132)` and `LIST CDS SYSMOD(TFTPDEL)` print after step 2,
before step 3 has run:

```
 LIST CDS  SYSMOD(TFTP132) .
SMPCDS   SYSMOD ENTRIES
  NAME
TFTP132   TYPE            = FUNCTION
          DELBY           = TFTPDEL

 LIST CDS  SYSMOD(TFTPDEL) .
SMPCDS   SYSMOD ENTRIES
  NAME
TFTPDEL   TYPE            = FUNCTION
          STATUS          = REC  APP  ACC
          FMID            = TFTPDEL
          DATE/TIME REC   = 26.301  12:42:08
                    APP   = 26.301  12:42:23
                    ACC   = 26.301  12:42:37
          DELETE VER(001) = TFTP100   TFTP110   TFTP120   TFTP130   TFTP131   TFTP132
```

## 5. Scratch the libraries

After step 2 the two libraries are empty, but they still exist. The allocation
job of the installation creates both with `DISP=(NEW,CATLG,DELETE)` and is
meant for names that are free. Delete them in a job of your own:

```
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
```

Each statement ends with `IDC0550I ENTRY (A) ... DELETED`.

**Do this only when you remove FTPD for good.** After an upgrade, these are the
data sets that the next release installs into; leave them. `FTPD.SAMPLIB` may
stay. The install job scratches it itself the next time.

## The fallback: UCLIN alone

Use this way when the dummy function cannot be used. After step 1, submit this
job instead of steps 2 and 3. It changes the two SMP data bases and no library.
In the first line of each block, name the FMID that the system carries
(`LIST CDS MOD(FTPD) .` shows it as `FMID`); the job names `TFTP132`:

```
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
```

Every `DEL` that finds its entry reports `HMA2550 UPDATE COMPLETE`. An id that
was never in the zone is reported as `HMA2532` and `HMA2560`, and the step ends
with return code 08. That is expected for the ids your system never had; the
statements after it are still processed. It is no sign that a line did what you
wanted, and a step that ends with return code 08 can also hide a statement that
failed for another reason. Read every message, and take the `LIST` as the only
proof. This is the output of step `UCLCDS` on a system with a new installation
of release 1.3.2. `TFTP131` is in the zone as the mark that the install left;
`TFTP130`, `TFTP120`, `TFTP110` and `TFTP100` were never in it:

```
 UCLIN CDS .
  DEL SYSMOD(TFTP132) MOD(FTPD) .
HMA2550    UPDATE COMPLETE FOR TFTP132
  DEL MOD(FTPD) .
HMA2550    UPDATE COMPLETE FOR FTPD
  DEL LMOD(FTPD) .
HMA2550    UPDATE COMPLETE FOR FTPD
  DEL SYSMOD(TFTP132) .
HMA2550    UPDATE COMPLETE FOR TFTP132
  DEL SYSMOD(TFTP131) .
HMA2550    UPDATE COMPLETE FOR TFTP131
  DEL SYSMOD(TFTP130) .
HMA2532 ** SYSMOD ENTRY TFTP130 TO BE DELETED DOES NOT EXIST
HMA2560    UPDATE PROCESSING TERMINATED - UPDATE NOT COMPLETE
  DEL SYSMOD(TFTP120) .
HMA2532 ** SYSMOD ENTRY TFTP120 TO BE DELETED DOES NOT EXIST
HMA2560    UPDATE PROCESSING TERMINATED - UPDATE NOT COMPLETE
  DEL SYSMOD(TFTP110) .
HMA2532 ** SYSMOD ENTRY TFTP110 TO BE DELETED DOES NOT EXIST
HMA2560    UPDATE PROCESSING TERMINATED - UPDATE NOT COMPLETE
  DEL SYSMOD(TFTP100) .
HMA2532 ** SYSMOD ENTRY TFTP100 TO BE DELETED DOES NOT EXIST
HMA2560    UPDATE PROCESSING TERMINATED - UPDATE NOT COMPLETE
 ENDUCL .
HMA2050    UCLIN PROCESSING COMPLETED - HIGHEST RETURN CODE IS 08
HMA2050    HMASMP PROCESSING COMPLETED - HIGHEST RETURN CODE IS 08
```

Step `UCLACDS` gives the same, without the `LMOD` statement.

**The two zones have a step each for a reason.** After a step has ended with
return code 08, SMP does not execute the next `UCLIN` of the same step: it
answers `HMA2293 ** CONTROL STATEMENT IGNORED DUE TO PREVIOUS ERROR` and does
nothing. With both blocks in one step, the ids that the system never had would
leave the ACDS untouched. Measured on a system with a new installation of
1.3.2, with both blocks of the job above in one step: the `UCLIN CDS` block
ended with return code 08 at `DEL SYSMOD(TFTP130)`, the `UCLIN ACDS` block that
followed was ignored, the step ended with return code 12, and
`LIST ACDS SYSMOD(TFTP132)` still showed the entry.

Then read the `LIST` as in step 4, here fourteen statements without `TFTPDEL` and
with the two module entries, and, if you remove FTPD for good, scratch the
libraries as in step 5. `UCLIN` changes only the inventory: until you scratch
them, the load module is still in the target library, and the copy that SMP
accepted is in the distribution library.

## You installed a development build with a later FMID

A development build of `main` is packaged under the FMID of the release that
`main` is heading for. A build of 1.4.0-dev carries `TFTP140`, although 1.4.0 is
not released. If you installed such a package by SMP, the system holds an id
that no release of this document knows. Neither the dummy function of step 2 nor
the lists of step 3 name it, so it stays in both zones, and it still owns the
load module. Install a release only after you have cleaned it up as described
here.

Ask SMP which id the system carries:

```
 LIST CDS MOD(FTPD) .
```

`FMID = TFTP140` (or any other id that is not in the table above) means that
you are in the right section.

The steps are those of the main way, with three differences. The release package
is on the system before FTPD stops, `TFTP140` is named in every list, and the
libraries stay.

### 1. Upload the release package first

FTPD is the way the package gets onto the system, and you stop it in the next
step. Upload both `.xmit` files of the release now, in binary, as described in
step 3 of [installation.md](installation.md), and edit the install job of the
package for your data set names. Do not run it yet.

### 2. Stop the server

```
/P FTPD
```

### 3. Delete the development build with the dummy function

Submit the job of [step 2](#2-delete-the-release-with-the-dummy-function) with
one change: `TFTP140` is added to the `DELETE` operand. Everything else, the
`HMASMP.LINKLIB` and `HMASMP.AFTPDLOD` statements, the `COND` on the later steps and the
member list at the end, stays as it is there.

**Continue the `++VER` card on a second line.** With `TFTP140` the card is 77
characters long. SMP rejects it: the `RECEIVE` ends with return code 08 and
prints `HMA2032 ** SYNTAX ERROR IN CONTROL STATEMENT AT COL 68`, which is the
start of the last id, and the job ends with 12. The job below breaks the card
after `TFTP132,` and continues it on the next line, which SMP accepts. Do not
type the card on one line.

```
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
```

The receive and the apply end with return code 04, the accept with 00, as in
step 2. The `APPLY` prints exactly one `HMA2461`, and it names the development
id:

```
 RECEIVE SELECT(TFTPDEL) .
----------++FUNCTION(TFTPDEL) .
----------++VER(Z038) DELETE(TFTP100,TFTP110,TFTP120,TFTP130,TFTP131,TFTP132,
----------            TFTP140) .
HMA3971    SYSMOD TFTPDEL HAS NO ELEMENTS
HMA3930    SYSMOD TFTPDEL SUCCESSFULLY RECEIVED
HMA2050    RECEIVE PROCESSING COMPLETED - HIGHEST RETURN CODE IS 04

 APPLY S(TFTPDEL) DIS(WRITE) .
HMA2240    SUCCESSFULLY DELETED LMOD FTPD ON LINKLIB LIBRARY
HMA2270    APPLY PROCESSING SUCCESSFULLY COMPLETED FOR SYSMOD TFTPDEL
HMA2461    SYSMOD TFTP140 NOT FOUND ON SMPSCDS LIBRARY
HMA2050    APPLY PROCESSING COMPLETED - HIGHEST RETURN CODE IS 04

 ACCEPT S(TFTPDEL) DIS(WRITE) .
HMA2240    SUCCESSFULLY DELETED MODULE FTPD ON AFTPDLOD LIBRARY
HMA2270    ACCEPT PROCESSING SUCCESSFULLY COMPLETED FOR SYSMOD TFTPDEL
HMA2050    ACCEPT PROCESSING COMPLETED - HIGHEST RETURN CODE IS 00
```

The member lists are empty, as in step 2. Read them as there: both libraries
must list no member.

**Check this step before you go on.** If the `RECEIVE` ended with 08, as it does
with the one-line card, the job removed nothing: the member lists of its last
step still name `FTPD` in both libraries. Do not run step 4 then, because
`UCLIN` changes only the inventory and leaves the load module where it is. Fix
the card and run this step again. Go on when the `APPLY` and the `ACCEPT` have
printed their `HMA2240` lines and both member lists are empty.

### 4. Free the ids, with `TFTP140` in the lists

Submit the job of [step 3](#3-free-the-ids) with `TFTP140` added to the `DEL`
statements of both `UCLIN` blocks and to the `LIST` statements. `TFTP140` comes
first, ahead of `TFTPDEL`:

```
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
```

Both `UCLIN` steps end with return code 00. Each zone reports eight
`HMA2550 UPDATE COMPLETE`, one per id, `TFTP140` among them:

```
 UCLIN CDS .
  DEL SYSMOD(TFTPDEL) .
HMA2550    UPDATE COMPLETE FOR TFTPDEL
  DEL SYSMOD(TFTP140) .
HMA2550    UPDATE COMPLETE FOR TFTP140
  DEL SYSMOD(TFTP132) .
HMA2550    UPDATE COMPLETE FOR TFTP132
  ...
  DEL SYSMOD(TFTP100) .
HMA2550    UPDATE COMPLETE FOR TFTP100
 ENDUCL .
HMA2050    UCLIN PROCESSING COMPLETED - HIGHEST RETURN CODE IS 00
```

The `UCLIN ACDS` step prints the same lines. On the test systems this job was
enough: no `HMA2532` or `HMA2560` appeared in either zone, and the `ACDS` needed
no statement beyond these.

Read the `LIST` as in [step 4](#4-read-the-list--this-is-the-actual-result):
sixteen statements, and every one must show its id as not found. On the test
systems every statement did, and the step ended with return code 04. A stanza with
`TYPE = FUNCTION` for `TFTP140` is an entry that is still there, in whichever
zone it shows.

### 5. Do not scratch the libraries

Leave out step 5. The release installs into `FTPD.LINKLIB` and `FTPD.AFTPDLOD`,
which are empty now.

### 6. Install the release

Run the install job of the release package that you prepared in section 1.
**Do not run the allocation job.** It allocates both libraries `NEW` and fails
on libraries that exist. The steps and the checks are those of the upgrade in
[installation.md](installation.md).

`RECEIVE`, `APPLY CHECK`, `APPLY` and `ACCEPT` all end with return code 00. The
`APPLY` prints no `HMA2461`. Both steps copy the module, and that is the line
to look for:

```
 APPLY S(TFTP132) DIS(WRITE) .
HMA2380    COPY SUCCESSFUL - MOD=FTPD - LMOD=FTPD - LIBRARY=LINKLIB - SYSMOD=TFTP132 - RETURN CODE=00
HMA2270    APPLY PROCESSING SUCCESSFULLY COMPLETED FOR SYSMOD TFTP132
HMA2050    APPLY PROCESSING COMPLETED - HIGHEST RETURN CODE IS 00

 ACCEPT S(TFTP132) DIS(WRITE) .
HMA2380    COPY SUCCESSFUL - MOD=FTPD - LMOD=FTPD - LIBRARY=AFTPDLOD - SYSMOD=TFTP132 - RETURN CODE=00
HMA2270    ACCEPT PROCESSING SUCCESSFULLY COMPLETED FOR SYSMOD TFTP132
HMA2050    ACCEPT PROCESSING COMPLETED - HIGHEST RETURN CODE IS 00
```

### 7. Check the result

List the members of `FTPD.LINKLIB` and `FTPD.AFTPDLOD`: `FTPD` must be in both.
Then `LIST CDS MOD(FTPD) .` must show the `FMID` of the release you installed,
and not `TFTP140`. Start FTPD and read the banner; it must name the release:

```
FTPD000I FTPD 1.3.2 (xxxxxxx) STARTING
```

On the test systems the two member lists name `FTPD` in both libraries:

```
 LISTDS 'FTPD.LINKLIB' MEMBERS
FTPD.LINKLIB
--MEMBERS--
  FTPD
 LISTDS 'FTPD.AFTPDLOD' MEMBERS
FTPD.AFTPDLOD
--MEMBERS--
  FTPD
```

`LIST CDS MOD(FTPD) .` and `LIST ACDS MOD(FTPD) .` each answer with the release
as `FMID` and `RMID`. This is the answer of the `CDS`:

```
 LIST CDS  MOD(FTPD) .
FTPD      LASTUPD         = TFTP132  TYPE=ADD
          LIBRARIES       = DISTLIB=AFTPDLOD
          FMID            = TFTP132
          RMID            = TFTP132
          LMOD            = FTPD
```

The server starts under the release (the line `FTPD062W` in the output below
depends on your parameter file):

```
  FTPD000I FTPD 1.3.2 (5FA9D0E) STARTING
  FTPD005I LIBC370 2.25.1 (FCDE4E8)
  ...
  FTPD001I FTPD 1.3.2 READY
```

**What has been measured.** The flow of this section was run on a test
system started from the TK5 image and on one started from the MVS/CE image,
each with its own run for the steps above and for the check that follows. The system
carried a development build of 1.4.0 (`TFTP140`) that was installed with its
own install job. Both systems gave the same return codes and the same messages
in every step, except where the text says otherwise.

**The development id keeps the module.** The check was to install release 1.3.2
over the development build without this section. `RECEIVE`, `APPLY` and `ACCEPT`
all end with return code 00, and the job prints no `HMA2380` line: SMP copies
nothing. The summary of the `APPLY` and of the `ACCEPT` lists `MOD FTPD` as `NOT SEL`. `LIST CDS MOD(FTPD)`
still names `TFTP140` as `FMID` and `RMID` in both zones, and the server that
you start announces itself as 1.4.0-DEV. Only the member lists, the `LIST` and
the banner show this. The return codes do not.

**What has not been measured.** A system with an older history, for example one
that holds ids from earlier releases that SMP lost track of, or one on which a
second development build was installed over the first.

## What is not removed, because SMP never owned it

- The procedure and the configuration member you copied into your PROCLIB and
  PARMLIB.
- The entry for `FTPD.LINKLIB` in the APF list, if you made one.
- Your RAKF definitions — the `FTPAUTH` profile, and any identity you mapped
  the started task `FTPD` to (releases before 1.3.0 switched to an `FTPD` user
  at startup). See [FTPD_RAKF_SETUP.md](FTPD_RAKF_SETUP.md).

Those are yours to delete.

---

## Why `RESTORE` and `REJECT` do not work

Worth knowing, because the messages point away from the cause. The install job
accepts the FMID in the same run in which it applies it. That closes both of the
usual ways back:

```
HMA2452 ** SYSMOD TFTP132 SELECTED FOR RESTORE HAS BEEN ACCEPTED
HMA2462 ** SYSMOD TFTP132 NOT FOUND ON SMPPTS LIBRARY
```

`RESTORE` refuses a SYSMOD that has been accepted (`HMA2452`, return code 12).
`REJECT` then fails for a reason that looks unrelated: `HMA2462` says that the
SYSMOD is not on the `SMPPTS` data set, although the job received it from there
(return code 12).

The cause, on the system where this was measured, is the option `PURGE=YES` in
the system entry of the `SMPPTS`. With it, SMP deletes a SYSMOD from the PTS as
soon as it has accepted it. The entry shows with `LIST PTS .`:

```
SMPPTS  SYSTEM ENTRY

SYSTEM    OPTIONS         = PAGELEN=0061  PEMAX=9999  PURGE=YES  REJECT=YES
```

Two things have not been measured: a system with `PURGE=NO`, and a `REJECT`
after the `ACCEPT` there. No `UCLIN` statement was found that changes the system
entry, so this document does not change it. It removes the FMID in ways that do
not need the PTS.

## Why the install job accepts at all

The `ACCEPT` fills the distribution library, which is the base a later
`RESTORE` of a **PTF** returns to. Without it, a `RESTORE` would delete the
module rather than revert it, because there would be no previous level to go
back to. The price is what this document is about: the FMID itself becomes
permanent by documented means.
