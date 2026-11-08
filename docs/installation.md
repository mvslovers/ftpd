# FTPD Installation Guide

This guide installs FTPD on an MVS 3.8j system (TK4-, TK5, MVS/CE, or a custom
Hercules build) from the release distribution archive.

The installation is managed by **SMP Release 4** — the SMP that ships with
MVS 3.8j, not SMP/E. That means the system keeps a record of what was
installed, and there is a supported way back out (see
[Removing FTPD](#11-removing-ftpd)).

## Getting help

**Report problems as a GitHub issue:**
<https://github.com/mvslovers/ftpd/issues>

That is the only place bug reports are tracked. Please include the two build
stamps FTPD writes at startup (`FTPD000I` and `FTPD005I`, see
[step 9](#9-start-and-verify)) — they identify the exact build — plus the
console messages and, for an install problem, the job output.

**Questions and general support** are on Discord:
<https://discord.gg/gaUAFKGCR>

---

One placeholder is used throughout:

| | |
|---|---|
| `<version>` | the release, e.g. `1.1.0` — it appears in every shipped file name |

It is already filled in inside the shipped jobs; you only need it to recognise
which file is which.

**The data set names carry no version.** `FTPD.LINKLIB` is `FTPD.LINKLIB` in
every release. Before 1.1.0 they were versioned (`FTPD.V1R0M2.LINKLIB`), which
meant two releases could sit side by side and it was easy to act on the wrong
one; now there is one installation and an upgrade replaces it.

---

## 1. What is in the archive

The package holds six files:

| File | What it is |
|------|------------|
| `README.md` | this guide |
| `ftpd-<version>-load.xmit` | the FTPD load module, as a TSO RECEIVE-ready XMIT |
| `ftpd-<version>-samplib.xmit` | the sample library: STC procedure and configuration pattern |
| `ftpd-<version>-alloc.jcl` | job `FTPDALC`: allocates the datasets SMP installs into. **Run it once**, for the first install; not for an upgrade |
| `ftpd-<version>-inst.jcl` | job `FTPDINS`: steps `DELOLD`, `RECV1`, `RECV2`, `RECV` (RECEIVE), `APPLYCHK` (APPLY CHECK) and `APPLY`. It **ends after the APPLY**: the function is applied, not accepted |
| `ftpd-<version>-accept.jcl` | job `FTPDACC`: steps `GUARD`, `NOTAPPL`, `ACCEPT` and `CLEANUP`. Submit it when you have tried the product |

Three jobs, run in this order: `FTPDALC` once, `FTPDINS`, and later `FTPDACC`.

FTPD is a single load module. The sample library holds two members, `FTPD`
(the started task procedure) and `FTPDPRM0` (the configuration pattern).

Where everything ends up:

```
FTPD.LINKLIB     the load module -- point STEPLIB here
FTPD.SAMPLIB     the patterns you copy from in step 7
FTPD.AFTPDLOD    SMP's distribution library, the base a RESTORE returns to
                 (empty after the install job, filled by the accept job)
FTPD.FTPDLOAD    the staging library the load module is received into;
                 scratched by the accept job
```

**The install job stops after the APPLY, on purpose.** ACCEPT is the point
without a way back: SMP refuses RESTORE and REJECT for an accepted FMID
(section 11). The header of the install job says that until the ACCEPT a RESTORE
can still take the function out again. That holds for a first install; **after
an upgrade a RESTORE is refused** (section 2a). So the install job does not make
the decision for you: try the product first, then submit `FTPDACC`.

What the jobs leave behind, measured on test containers (TK5 and MVS/CE) with the
published package of 1.3.4 (built by mbt 3.0.1). The first column is a first
install, the second an upgrade over an accepted 1.3.3:

| | After `FTPDINS`, first install | After `FTPDINS`, upgrade | After `FTPDACC` |
|---|---|---|---|
| Job end | CC 0000: every step RC 00 | CC 0004: `DELOLD`, `RECV1`, `RECV2`, `RECV`, `APPLYCHK` end RC 00, `APPLY` ends RC 04 (`HMA2461`, see [section 2a](#2a-upgrading-from-130-131-132-and-133)) | CC 0000: `GUARD`, `ACCEPT`, `CLEANUP` end RC 00, `NOTAPPL` is not run |
| `TFTP134` in the target zone (CDS) | `REC APP` | `REC APP` | `REC APP ACC` |
| `TFTP134` in the distribution zone (ACDS) | not found | not found | `REC APP ACC RGN` |
| `TFTP133` to `TFTP130` | tombstones in the CDS (`DELBY = TFTP134`), none in the ACDS | `TFTP133`: tombstone in the CDS; in the ACDS still `REC APP ACC RGN` | tombstones (`DELBY = TFTP134`) in both zones, no `RGN` in the ACDS |
| `FTPD.LINKLIB` | `FTPD` | the new `FTPD` | the new `FTPD` |
| `FTPD.AFTPDLOD` | empty | still the module of 1.3.3 | the new `FTPD`, the same as in `FTPD.LINKLIB` |
| `FTPD.FTPDLOAD` (staging) | present | present | scratched |

The tombstones appear on a first install too: the SYSMOD names the four
predecessors it deletes, and SMP records them whether or not they were ever
installed. The first install was run on TK5 and on MVS/CE, the upgrade on TK5
(1.3.3, installed from its published package, to 1.3.4), and the accept job on both
(on MVS/CE after a first install, on TK5 after the upgrade); the last column is the
same in every case.

**The accept job.** Nothing in it needs editing. `GUARD` runs
`LIST CDS SYSMOD(TFTP134)`. Only when that ends RC 00 does `ACCEPT` run, and
`CLEANUP` scratches `FTPD.FTPDLOAD` only when `GUARD` and `ACCEPT` both ended RC
00. The gate is the entry of `TFTP134` in the target zone, not the return code of
the install job: after an upgrade the install job ends CC 0004, and the accept job
still runs through. The header of the accept job says to submit it after an install
job whose APPLY CHECK and APPLY ended RC 0, or RC 4 for `HMA2461` (a predecessor in
delete) or `HMA4441` alone, and not after RC 8 or 12. The `ACCEPT` step prints
`HMA2380 COPY SUCCESSFUL ... LIBRARY=AFTPDLOD` for `FTPD`.

**Submitting the accept job a second time** ends the job with `JCL ERROR`:
`IEF212I ... FTPDLOAD - DATA SET NOT FOUND`, because the first run scratched the
staging library. `GUARD` ends RC 00 and no step after it runs, so the second run
changes nothing.

**Submitting it when `TFTP134` is not applied** (the install job never ran, or
the function was taken out again by a RESTORE or by the removal job): `GUARD` ends
RC 04 (the entry is not in the target zone), `NOTAPPL` ends RC 08, and `ACCEPT`
and `CLEANUP` do not run. The job ends CC 0008, and nothing is accepted or
scratched. Measured after a removal.

**The accept job may run while FTPD is up.** Measured on MVS/CE: FTPD 1.3.4 was
started from the applied-only `FTPD.LINKLIB`, the accept job ran through (CC
0000), and `F FTPD,STATS` afterwards answered `FTPD010I STATUS: ACTIVE`. The
accept job does not write `FTPD.LINKLIB`.

**Not measured:** the accept job after an APPLY that printed `NOT SEL`; a system
where `PURGE=NO` is set in the PTS options; an upgrade from 1.3.0, 1.3.1 or
1.3.2 straight to 1.3.4 (only 1.3.3 to 1.3.4 was run); and the accept job on a
system where `TFTP134` is received but not applied.

**Verify the install by listing the members of `FTPD.LINKLIB` (and, after the
accept job, of `FTPD.AFTPDLOD`), never by the condition codes.** SMP keys element
ownership on `MOD(name)`, and a SYSMOD that does not own an element skips it: the
element summary then prints

```
TYPE   NAME      STATUS
MOD    FTPD      NOT SEL
```

nothing is copied, and every other line still says success. The message that
separates a real install from that one is:

```
HMA2380 COPY SUCCESSFUL - MOD=FTPD - LMOD=FTPD - LIBRARY=...
```

If that line is not in the job log, nothing was copied. The line for
`LIBRARY=LINKLIB` is in the `APPLY` step of `FTPDINS`; the one for
`LIBRARY=AFTPDLOD` is in the `ACCEPT` step of `FTPDACC`. (The `NOT SEL` case was run with the package of 1.3.2, whose install job also accepted, see the removal guide; not with this package.)

After the accept job, check three more things: `FTPD.AFTPDLOD` holds the member
`FTPD`, `FTPD.FTPDLOAD` is gone, and

```
//LIST    EXEC SMPAPP
//SMPCNTL  DD  *
 LIST CDS  SYSMOD(TFTP134) .
 LIST ACDS SYSMOD(TFTP134) .
/*
```

shows `TFTP134` as `REC APP ACC` in both zones.

---

## 2. Prerequisites

- **MVS 3.8j** on Hercules, up and IPLed, with SMP 4 usable (the `SMPREC` and
  `SMPAPP` procedures and the `SYS1.SMP*` datasets — present on TK4-/TK5 and
  MVS/CE as shipped).
- **RAKF**, for authentication — see below. This one is not optional. The
  release matters for one thing only: letting users see the jobs of other
  users takes RAKF 2.0.0 or later.
- **UFSD**, only if you want the UFS namespace — see below.
- A userid authorised to submit jobs, to update a PROCLIB in the started-task
  concatenation, and to update a PARMLIB.
- A way to upload a **binary** file to the host — see step 3.
- Roughly 15 cylinders of DASD: 10 for the two libraries the allocation job
  creates, the rest for the two TSO RECEIVE targets. One of those, the staging
  library `FTPD.FTPDLOAD`, is scratched again by the last step of the accept
  job; until you submit that job it stays.

### UFSD is optional

FTPD reaches three namespaces — MVS datasets, JES, and the UFSD filesystem
(step 10). **Only the third needs UFSD, and nothing about the install does.**
The SYSMOD declares no prerequisite, `libufs` is linked statically into the load
module, and FTPD asks UFSD for a session lazily, the first time a client
actually uses a UFS path. On a system without the UFSD started task those
commands answer

```
550 UFS service not available
```

and everything else works normally. Install FTPD now and add UFSD later if you
want it: <https://github.com/mvslovers/ufsd/releases>, with its own guide at
[ufsd/docs/installation.md](https://github.com/mvslovers/ufsd/blob/main/docs/installation.md).

Note that UFSD has to be **running**, not just installed — it is a started task
FTPD talks to across address spaces.

FTPD 1.3.1, 1.3.2, 1.3.3 and 1.3.4 carry `libufs` from UFSD 1.5.2; 1.3.0 carried the one from UFSD
1.4.0. `libufs` is linked into the load module, so a new FTPD build is the only
way its fixes reach FTPD. The library reports a write that the server
rejected, and it sends a block again when the server's buffer pool is empty,
which a parallel upload could hit.

**If you use the UFS view, install UFSD 1.5.2.** UFSD 1.5.1 and earlier
releases could give one disk block to two files, or twice to the same file,
while a file was being written, and reported no error. A file written this way
holds foreign data (measured on 1.4.0 and 1.5.1). The fix is in the UFSD
server, not in `libufs`: a new FTPD does not help, the server must be
upgraded. UFSD 1.5.2 stops the damage, it does not undo it. A file that was
damaged before stays damaged. Compare such files with a copy, or write them
again after the upgrade. `F UFSD,REBUILD` is not a repair. The release notes of
UFSD 1.5.2 have the details.

### RAKF is required for logins

FTPD has no user database of its own. Every `USER`/`PASS` is verified by RAKF
(RACINIT, SVC 244), and every dataset access is checked against the RAKF
`DATASET` class under the logged-in user's identity. **On a system without RAKF
nobody can log in**, and no configuration option turns that off.

The setup — the identity of the started task and the `FTPAUTH` FACILITY
profile — is step 8, and the full reference is
[FTPD_RAKF_SETUP.md](https://github.com/mvslovers/ftpd/blob/v1.3.4/docs/FTPD_RAKF_SETUP.md).

**FTPD 1.3.3 does not need RAKF 2.0.0 to run.** On a RAKF without the class
`JESSPOOL` (RAKF 1.2.x has none) it installs and starts, and in the JES view
every user sees, reads and cancels only the own jobs. **RAKF 2.0.0 or later is needed to let users see the
jobs of other users:** that is done with profiles of the RAKF class
`JESSPOOL`, which RAKF 2.0.0 introduced. Section 7 of the RAKF setup guide
explains the profiles, and what RAKF does on a system without the class.

The identity that the started task `FTPD` runs under must be in a group that
has READ on `FACILITY SVC244`, such as `STARTED` in the shipped `users.txt`.
Otherwise FTPD cannot obtain APF authorization (`RAKF000A` for `FACILITY
SVC244`, `FTPD003W APF SETUP FAILED RC=4`).

### Authorisation

FTPD is link-edited `AC(1)` and needs to be APF-authorised for the ACEE
switch that makes each session's dataset I/O run under the client's identity
rather than the server's.

It obtains authorisation itself at startup via `clib_apf_setup()`, which goes
through **SVC 244 — and that comes from RAKF**, which you need anyway. The
clean alternative is to add `FTPD.LINKLIB` to the APF list in
`SYS1.PARMLIB(IEAAPF00)`. On MVS 3.8j the APF list is only read at IPL — but
since 1.1.0 the library name no longer carries the version, so that is **one
IPL ever**, not one per release. Before 1.1.0 every release needed its own
entry and its own IPL.

APF on 3.8j is keyed by data set name **and volume serial** — an `IEAAPF00`
entry reads `dsname volser`, not a name alone. So an entry naming
`FTPD.LINKLIB` stops covering it if the library is ever moved to another
volume.

**The same rule bites the other way round, and that is the direction that
catches people.** If the entry is already there — added ahead of the install,
or left from a previous release — it does not merely *permit* a volume, it
**dictates** one. The allocation job of 1.3.4 never lets `SYSDA` pick: it
allocates `FTPD.LINKLIB` on the volume `TGTVOL` names and `FTPD.AFTPDLOD` on
`DLBVOL` (defaults `MVSRES` and `SMP000`, the volumes of an MVS/CE system; on TK5
`TGTVOL=TK5RES,DLBVOL=TK5DLB`, see step 4). It does not read the APF list and does
not say if the volumes differ. So set `TGTVOL` to the volume in the entry. If you
do not, you get a library that exists, holds the right module, passes every check
in section 6 — and is not authorised. (The allocation job of 1.3.3 and earlier
said `UNIT=SYSDA` and no `VOL=SER`, which put the library wherever `SYSDA` picked.)

Nothing reports the mismatch, because FTPD recovers from it: `clib_apf_setup()`
falls back to SVC 244 and the server comes up authorised anyway. The only place
the two are distinguishable is the startup message below — which is why
`AUTHORIZED BY SVC` **on a system where you added an APF entry** is not a site
decision but a symptom. Measured on a test system with an `IEAAPF00` entry for
a data set that did not exist yet and the allocation job of 1.3.3, free to put it
anywhere.

The two routes are not equivalent in one respect that matters if you ever have
to debug FTPD. An **APF entry** authorises the job step *before* program fetch,
so MVS loads FTPD into subpool 252 **key 0** — authorised code must not be
patchable by problem-key code. **SVC 244** sets `JSCBAUTH` *after* the fetch and
cannot relabel storage that is already allocated, so the module stays key 8.
FTPD runs problem state key 8 either way, which means that under an APF entry it
must never store into its own module storage. It does not, as of 1.0.1: the
server block, the log level and the trace ring all live in `main()`'s storage
and are reached through the C runtime's process anchor. **On 1.0.0 an APF entry
kills FTPD outright** — see the `S0C4` row under Troubleshooting.

You do not have to work out which route you got: FTPD says so at startup, on
the line after the version banner.

```
FTPD008I AUTHORIZED BY LIBRARY (MODULE KEY 0)     APF list entry
FTPD008I AUTHORIZED BY SVC (MODULE KEY 8)         SVC 244, from RAKF
```

The key is inferred from the route rather than measured — an authorised job
step has its module fetched key 0, an unauthorised one key 8, and SVC 244
arrives too late to change either. Neither line is a warning *in itself*: both
routes end in an authorised started task, and which one you want is a site
decision.

With one exception, from the paragraph above. If you put `FTPD.LINKLIB` in the
APF list and IPL'd for it, then `AUTHORIZED BY SVC (MODULE KEY 8)` means the
entry is not covering the library — almost always because the allocation job
placed it on a different volume than the entry names. Without RAKF there is no
SVC 244 to fall back on and the start fails outright, which at least says so;
with RAKF you get a working server and no indication at all.

Unlike UFSD, FTPD **warns and keeps running** when authorisation fails:

```
FTPD003W APF SETUP FAILED RC=n
```

The server starts and accepts connections, but nobody can log in. Every
`PASS` is answered

```
530 Login not possible: FTPD is not APF authorized. Ask the administrator to check the FTPD003W console message.
```

The session stays open and usable (`USER`, `SYST`, `NOOP`, `QUIT`); the
attempt does not count as a failed login, so the session is not closed after
three of them. The commands that need a login answer `530 Not logged in.`
The log (`STDOUT`) gets one `[ERROR]` line for it per run of the server.
Before 1.3.0 `PASS` was not answered at all, and the session was closed after
three of them with `FTPD071E` and no reply. Treat `FTPD003W` as an install
that is not finished.

---

## 2a. Upgrading from 1.3.0, 1.3.1, 1.3.2 and 1.3.3

A fresh install can skip this section. 1.3.4 deletes `TFTP133`, `TFTP132`,
`TFTP131` and `TFTP130`, so a system on any of 1.3.0, 1.3.1, 1.3.2 and 1.3.3
upgrades in one step. Ask SMP first which FMID owns the load module, under the
SMP procedure of your system:

```
//LIST    EXEC SMPAPP
//SMPCNTL  DD  *
 LIST CDS  MOD(FTPD) .
 LIST ACDS MOD(FTPD) .
/*
```

| Owner of `MOD(FTPD)` | What to do |
|---|---|
| `TFTP133`, `TFTP132`, `TFTP131` or `TFTP130` | 1.3.3, 1.3.2, 1.3.1 or 1.3.0. Upgrade with this package, as described below. If you use the UFS view, read the note on UFSD 1.5.2 below. |
| `TFTP120` | 1.2.0. Install 1.3.0 first, with the [1.3.0 package](https://github.com/mvslovers/ftpd/releases/tag/v1.3.0), then come back. The changes of 1.3.0 that need a decision are listed at the end of this section. |
| `TFTP110`, `TFTP100` | Upgrade release by release, see the end of this section. |
| none | FTPD is not installed through SMP on this system. This is a fresh install; on MVS/CE too (see step 5). |

On a system where an older id owns the load module (`TFTP120` and earlier), the
1.3.4 install copies **nothing** and still ends `RC 00`: it deletes `TFTP133`,
`TFTP132`, `TFTP131` and `TFTP130` and no other id, and SMP keys the ownership
of the load module on the module name.

`TFTP130`, `TFTP131`, `TFTP132` and `TFTP133` stay in the SMP zones after the
install as entries that only say `DELBY = TFTP134` or `DELBY = TFTP133`, also
the ones your system never had. That is how SMP records a deleted id; they are
not an install, and nothing needs to be done about them.

### From 1.3.0, 1.3.1, 1.3.2 or 1.3.3

The upgrade is a **drop-in** as far as SMP and the data sets go: 1.3.4
installs into the same data sets, under the same started task procedure and
APF entry, and reads the same `FTPDPRM0`. Nothing needs a decision from you
before you install.

| | 1.3.0, 1.3.1, 1.3.2, 1.3.3 | 1.3.4 |
|---|---|---|
| FMID | `TFTP130`, `TFTP131`, `TFTP132`, `TFTP133` | `TFTP134`, deletes `TFTP133`, `TFTP132`, `TFTP131` and `TFTP130` |
| Libraries | `FTPD.LINKLIB`, … | unchanged |
| C library | LIBC/370 2.25.1 | LIBC/370 2.29.2 |
| `libufs` | UFSD 1.4.0 (1.3.0), UFSD 1.5.2 (1.3.1, 1.3.2, 1.3.3) | UFSD 1.5.2 |
| RAKF | 1.2.x or 2.0.0 | unchanged |

1.3.4 is a patch release and adds no function. What changes in behaviour is in
the table *What Is New in FTPD 1.3.4* in the User's Guide (the book that comes
with the release). For a system that comes from 1.3.2 or earlier, the tables for
1.3.3, 1.3.2, 1.3.1 and 1.3.0 in the same chapter apply as well. Of 1.3.4, in
short:

- A text `put` into a spanned data set (`VBS`, or `VB` with a block size
  smaller than the record length plus 4) stores every record. Up to 1.3.3, a
  record that crossed a block boundary lost its tail, and FTPD still answered
  `250`. A spanned data set that was received in text mode by 1.3.3 or an
  earlier release may be incomplete; upload the file again.
- The longest text line for a spanned data set is the record length minus 8
  bytes; a longer line is refused with `451`, or cut with `SITE TRUNCATE`.
- A binary or EBCDIC upload into a spanned data set is accepted.
- In the JES view, `LIST JOBnnnnn` of a job with a procedure shows other
  values in the columns `STEPNAME` and `PROCSTEP`.
- The C library is LIBC/370 2.29.2, which the repair of the spanned data sets
  needs; it has no effect on the installation.

**If you use the UFS view, you need UFSD 1.5.2 or later.** On 1.3.0 that is
a change, because 1.3.0 carried the `libufs` of UFSD 1.4.0. UFSD 1.5.1
and earlier can give one disk block to two files while writing, without an
error, and a written file then holds foreign data. UFSD 1.5.2 stops this, but
does not repair files that are already damaged. How to install and upgrade UFSD
is in its own guide
([ufsd/docs/installation.md](https://github.com/mvslovers/ufsd/blob/main/docs/installation.md)).
FTPD without UFSD is not affected.

The upgrade to 1.3.4 was measured on a TK5 test container that went through the
upgrade chain 1.2.0, 1.3.0, 1.3.3 and 1.3.4, each package installed through
SMP, the last one with the published package of 1.3.4. The install job ended CC
0004: `DELOLD`, `RECV1`, `RECV2`, `RECV` and `APPLYCHK` RC 0, `APPLY` RC 4 with
`HMA2461 SYSMOD TFTP133 NOT FOUND ON SMPSCDS LIBRARY`. The accept job then ended
CC 0000: `GUARD`, `ACCEPT` and `CLEANUP` RC 0, `NOTAPPL` not run. The upgrade from
1.3.0, 1.3.1 and 1.3.2 directly to 1.3.4 was not measured; the SYSMOD
of 1.3.4 names those ids in its delete list all the same.

### The steps

1. **Stop FTPD** (`/P FTPD`, step 5).
2. **Skip the allocation job** (step 4) — if `FTPD.LINKLIB` and
   `FTPD.AFTPDLOD` exist, which they do after an SMP install of 1.3.0, 1.3.1,
   1.3.2 or 1.3.3. The job allocates them `NEW`: its `CHECK` step looks in the
   catalog first and ends RC 8 on an existing library, so nothing is allocated
   twice (step 4). `LISTCAT LEVEL(FTPD)` (or ISPF 3.4 on `FTPD.*`) tells which
   case you are in.
3. **Run the install job** (step 6). It ends CC 0004: the `APPLY` ends
   `COND CODE 0004` on `HMA2461 SYSMOD TFTP13x NOT FOUND ON SMPSCDS LIBRARY`,
   where `TFTP13x` is the id that was installed (`TFTP130`, `TFTP131`, `TFTP132`
   or `TFTP133`). That is the expected result of a replacement; see *One return
   code that looks wrong and is not* in section 2b. The job **stops after the
   APPLY**: the new module is in `FTPD.LINKLIB`, and `FTPD.AFTPDLOD` still holds
   the old one.
4. **Start FTPD** (`/S FTPD`) and read the banner. It must name the new
   release:

   ```
   FTPD000I FTPD 1.3.4 (xxxxxxx) STARTING
   FTPD005I LIBC370 2.29.2 (xxxxxxx)
   ```

   `FTPD 1.3.0`, `1.3.1`, `1.3.2` or `1.3.3` on the first line means the
   started task still runs the old load module. Check the member list of
   `FTPD.LINKLIB` and the job log for `HMA2380 COPY SUCCESSFUL` — not the
   condition codes.
5. **Try the product, then run the accept job** (`FTPDACC`, section 1). It ends
   CC 0000, scratches `FTPD.FTPDLOAD` and makes `TFTP134` the base for any later
   RESTORE. It does not need FTPD to be stopped (measured on MVS/CE with FTPD
   running). Until it has run, the system holds `TFTP134` as applied and `TFTP133`
   as the accepted level in the distribution zone.

**Until the accept job has run, a RESTORE does not take the upgrade back.** The
header of the shipped install job says that until the ACCEPT a RESTORE can still
take the function out again. After an upgrade over an accepted `TFTP133` that is
not so. Measured with `RESTORE S(TFTP134)` after the install job of the upgrade
above, with the new module still in `FTPD.LINKLIB`:

```
HMA2452 ** SYSMOD TFTP134 SELECTED FOR RESTORE DELETES SYSMODS
HMA3703 ** RESTORE PROCESSING TERMINATED BECAUSE FUNCTION SYSMOD TFTP134 FAILED
```

The RESTORE ended RC 12 and changed nothing. So the way back from an applied
`TFTP134` over `TFTP133` is not a RESTORE. Either keep `TFTP134` and accept it,
or remove FTPD as the removal guide describes (installing the old release
afterwards was not run). On a first install, where nothing was deleted, a RESTORE does take the
function out (measured: RC 0, `HMA2240` deleted the load module from
`FTPD.LINKLIB`).

### From 1.2.0: install 1.3.0 first

1.3.4 deletes `TFTP133`, `TFTP132`, `TFTP131` and `TFTP130`, not `TFTP120`, so a
system on `TFTP120` installs the 1.3.0 package first (the README of the package describes
the steps; it deletes `TFTP120`), and then this one. Two changes of 1.3.0 need a decision from you **before** that install:

1. **Create the `JESSPOOL` profiles you need.** In 1.2.0 every user could
   list, read and cancel the jobs of everybody through the JES view
   (`SITE FILETYPE=JES`). Since 1.3.0 a user sees, reads and cancels only the
   own jobs; the jobs of other users need a profile in the RAKF class
   `JESSPOOL`, which exists from RAKF 2.0.0. Without any profile that is the
   state after the upgrade. If an administrator or an operator relies on seeing
   all jobs, create the profiles first, and update RAKF to 2.0.0 or later if it
   is older: a RAKF 1.2.x has no such class, the upgrade itself works, and
   every user sees only the own jobs. Section 7 of the
   [RAKF setup guide](https://github.com/mvslovers/ftpd/blob/v1.3.4/docs/FTPD_RAKF_SETUP.md)
   has the profiles and examples.
2. **Check the identity of the started task** (step 8). 1.2.0 logged on as
   `FTPD`/`USER` at startup. Since 1.3.0 FTPD keeps the identity RAKF gives the
   task and names it in `FTPD004I`. On RAKF 2.0.0, which maps by STC name, the
   userid `FTPD` in `USERS` is that mapping, and its group needs READ on the
   `FACILITY` profile `SVC244`. On a RAKF without such a mapping the task now
   runs as `STC`/`STCGROUP`, with Operations authority.

The `HASPCKPT` and `HASPACE1` statements of your procedure are optional since
1.3.0: FTPD finds the JES2 checkpoint and the spool itself (step 7). The table
*What Is New in FTPD 1.3.0*, in the same book, lists every other change of that
release. A `FTPDPRM0` of 1.2.0 that still has a `JESINTERFACELEVEL`, an
`AUTHUSER` or a volume line makes the start print `FTPD062W … SETTING(S) IN
DD:FTPDPRM HAVE NO EFFECT AND WERE IGNORED`. The lines did nothing before
either; delete them.

> **MVS/CE is a first install, not an upgrade, as far as SMP and the data sets
> go.** The FTPD 1.2.0 that comes with the image is not an SMP install: no data
> set `FTPD.*` exists, SMP has never seen `TFTP120`, and the started task runs
> from `MVSLOVER.FTPD.LOADLIB`. On MVS/CE do not skip the allocation job.
> Install this release as a fresh install: sections 3 to 9 in order. Section 5
> and step 7 say what is different. Read the two decisions above too: the
> changes of behaviour apply to you.

### Coming from 1.1.0 or 1.0.x

Upgrade release by release. A 1.1.0 system installs the
[1.2.0 package](https://github.com/mvslovers/ftpd/releases/tag/v1.2.0)
(`TFTP120` deletes `TFTP110`, banner `LIBC370 2.10.3`), then the 1.3.0
package, then this one. A 1.0.x system goes to 1.1.0 first, following section 2b with the
1.1.0 package from the
[releases page](https://github.com/mvslovers/ftpd/releases/tag/v1.1.0).

---

## 2b. Upgrading from 1.0.x to 1.1.0

This section describes the 1.1.0 install. It is kept here because that is the
step a 1.0.x system has to take first (see the end of section 2a).

A fresh install can skip this section. An upgrade from any 1.0.x release cannot:
**1.1.0 changes both the FMID and the data set names.**

| | 1.0.x | 1.1.0 |
|---|---|---|
| FMID | `TFTP100` | `TFTP110` |
| Libraries | `FTPD.V1R0M1.LINKLIB`, … | `FTPD.LINKLIB`, … |

**The FMID is handled for you.** If you upgraded before, or read an earlier
version of this guide, you will remember a `UCLIN` job that had to run first to
cut the old FMID out of the SMP inventory. **That step is gone.** The 1.1.0
SYSMOD carries `++VER(Z038) DELETE(TFTP100)`, and SMP transfers the element
ownership during the `APPLY`. Nothing has to be prepared, and the old
installation does not have to be removed first.

**The data sets are not handled for you, and cannot be.** That is the whole
content of this section, so it is worth understanding rather than following.

### Why SMP cannot clean up the old libraries

SMP does not record which *data set* an element lives in. It records the
**ddname** the install job used:

```
SMPCDS  LOAD MODULE ENTRIES
TTMPMOD   LASTUPD         = TTMP113  TYPE=ADD
          SYSTEM LIBRARY  = OLDLIB          <- a ddname, not a DSN
```

Both the 1.0.x and the 1.1.0 libraries end in `LINKLIB`, and the ddname comes
from that last qualifier — so both installs use ddname `LINKLIB`, pointing at
`FTPD.V1R0M2.LINKLIB` in the one case and `FTPD.LINKLIB` in the other. When
1.1.0's `APPLY` deletes the predecessor's load module, it resolves ddname
`LINKLIB` **in its own job**, which points at the new library. It deletes from
there, reports success, and copies the new module in:

```
HMA2240 SUCCESSFULLY DELETED LMOD FTPD ON LINKLIB LIBRARY
HMA2380 COPY SUCCESSFUL - MOD=FTPD - LMOD=FTPD - LIBRARY=LINKLIB - ...
```

`FTPD.V1R0M2.LINKLIB` is never opened. It keeps its copy of the old, still
APF-authorised `FTPD` module, and nothing in the job log mentions it.

Measured on a test system with throwaway ids and a throwaway module name,
in exactly this shape — same ddnames, different data sets (one job installs
the predecessor, a second upgrades over it, a third reads the result). The
upgrade completed: the new SYSMOD `REC APP
ACC` in both zones, `MOD` carrying the new `FMID` and `RMID`, both new
libraries holding the module — **and the old target library still holding its
own copy.**

So the two steps that used to look like housekeeping are the ones that matter
now.

### The procedure

The order matters: the old libraries are scratched **last**, after the banner
has told you the new release is what runs. Until then the old library is your
way back.

1. **Stop FTPD** (`/P FTPD`).
2. **Run the 1.1.0 allocation job** (step 4 below), then the **install job**
   (step 6). No `UCLIN`, nothing to remove first.
3. **Repoint the started task procedure.** The `STEPLIB` you copied at the last
   install names `FTPD.V1R0M?.LINKLIB`. **This is the step that decides whether
   you are running 1.1.0 at all.** While the old library still exists it still
   holds a working module: FTPD starts, serves, and logs no error — it is
   simply the old one. Change it to `FTPD.LINKLIB`.
4. **Replace the APF entry** if you use one: it names the old library, and the
   new library needs an entry of its own. Edit the old `IEAAPF00` line into
   the new one rather than adding a second line, so no entry for a library that
   is about to be scratched stays behind. The list is read at IPL only, so the
   entry takes effect at the next IPL; until then FTPD authorises through SVC
   244 (RAKF) as before. The old library name stays APF-authorised until that
   IPL, so do not reuse it for anything before then. See *Authorisation* above —
   this is the last time it will need changing, because the new name no longer
   carries a version.
5. **Start FTPD and check the banner** as described under *Verifying* below. It
   must say 1.1.0. If it does not, stop here: the old libraries are untouched.
6. **Scratch the old libraries**, and only now. Their names carry the patch
   level of whatever you installed, so look them up rather than assuming: ISPF
   3.4 on `FTPD.*`, or `LISTCAT LEVEL(FTPD)`. Expect three —
   `FTPD.V1R0M?.LINKLIB`, `.AFTPDLOD`, `.SAMPLIB`:

   ```
     DELETE FTPD.V1R0M2.LINKLIB  NONVSAM SCRATCH PURGE
     DELETE FTPD.V1R0M2.AFTPDLOD NONVSAM SCRATCH PURGE
     DELETE FTPD.V1R0M2.SAMPLIB  NONVSAM SCRATCH PURGE
   ```

   They are invisible to SMP now — it has moved on to the new names — so
   nothing will ever report them again, and nothing but you will remove them.

### Verifying, since the job log will not tell you

`FTPD005I` at startup names the release and the library it was built against:

```
FTPD000I FTPD 1.1.0 (XXXXXXX) STARTING
FTPD005I LIBC370 1.0.6 (XXXXXXX)
```

`LIBC370 1.0.4` on that second line means the started task is still loading the
1.0.2 module — step 3 did not take and the `STEPLIB` still points at the old
library. Fix that before you scratch anything.

And before that, check the new library really holds the module:

```
//LIST    EXEC PGM=IEHLIST
//SYSPRINT DD  SYSOUT=*
//DD1      DD  UNIT=SYSDA,VOL=SER=your-volume,DISP=SHR
//SYSIN    DD  *
 LISTPDS DSNAME=FTPD.LINKLIB,VOL=SYSDA=your-volume
/*
```

`SYSDA` runs on TK5 and on MVS/CE. `SYSALLDA` runs on MVS/CE only: on TK5 the
step ends in `IEH106I UNAVAILABLE DEVICE TYPE OR VOLUME I.D. SPECIFIED`. The
device type of the volume also works, in both places, as long as the system has
that type: `3390` for volume `TSO001` on TK5, `3350` for volume `WORK01` on
MVS/CE. `3390` on MVS/CE is a JCL error.

The directory lists one member, `FTPD`. `AMBLIST LISTLOAD OUTPUT=MODLIST`
on it reports `APFCODE 00000001` with `RENT`/`REUS` intact: `AC(1)` survives
SMP's `COPY`, so the library only needs to be APF-authorised.

### One return code that looks wrong and is not

On an upgrade the `APPLY` step of the install job ends **`COND CODE 0004`**, not
`0000`, and so does the job:

```
HMA2240    SUCCESSFULLY DELETED LMOD FTPD ON LINKLIB LIBRARY
HMA4180    INLINE JCLIN PROCESSING SUCCESSFUL FOR SYSMOD=TFTP134
HMA2380    COPY SUCCESSFUL - MOD=FTPD - LMOD=FTPD - LIBRARY=LINKLIB - SYSMOD=TFTP134 - RETURN CODE=00
HMA2270    APPLY PROCESSING SUCCESSFULLY COMPLETED FOR SYSMOD TFTP134
HMA2461    SYSMOD TFTP133 NOT FOUND ON SMPSCDS LIBRARY
HMA2050    APPLY PROCESSING COMPLETED - HIGHEST RETURN CODE IS 04
```

The excerpt was measured on the upgrade from 1.3.3 to 1.3.4 with the published
package of 1.3.4, on a TK5 test container with 1.3.3 installed from its own
published package. The return codes of the install job were `DELOLD` 0, `RECV1`
0, `RECV2` 0, `RECV` 0, `APPLYCHK` 0 and `APPLY` 4. The accept job that followed
printed `HMA2380 COPY SUCCESSFUL ... LIBRARY=AFTPDLOD` for the `ACCEPT` and ended
CC 0000. On the upgrades from 1.3.2, 1.3.1 and 1.3.0 to 1.3.3 (measured with the
published package of 1.3.3, which was one job) the `APPLY` also ended with
`COND CODE 0004`, and `HMA2461` named `TFTP132`, `TFTP131` and `TFTP130`.
`HMA2240` is the old module being deleted from `FTPD.LINKLIB` before the new one
is copied.

`HMA2461` is SMP noting that the deleted level has no backup entry to keep — it
never will, because it was deleted rather than superseded. The `APPLY` itself
succeeded, which `HMA2270` says. The accept job expects this: its `GUARD` looks for
`TFTP134` in the target zone and not at the return code of the install job, so the
job runs through after CC 0004 (it was run that way). Its header names RC 4 for
`HMA2461` as the normal end of an upgrade, and RC 8 or 12 as the reason not to
submit it. A *fresh* install has nothing to delete and ends RC 00 (measured with the
1.3.4 package on TK5 and on MVS/CE).

---

## 3. Upload the two XMIT files

Both `.xmit` files are EBCDIC NETDATA streams. Upload them **in binary** — no
ASCII/EBCDIC translation, no CRLF conversion — into sequential datasets with
`RECFM=FB LRECL=80 BLKSIZE=3120`.

**The dataset names are yours to choose.** The install job names them on its
own `RECEIVE` commands, so nothing depends on what you call them; you will
enter them once in step 6. This guide uses `IBMUSER.FTPD.LOAD.XMIT` and
`IBMUSER.FTPD.SAMP.XMIT`.

Whether you have to allocate them first depends on the upload path: **FTPD**
itself and **IND$FILE** create the dataset from the attributes you supply,
mvsMF and most other FTP servers need it to exist. Where a method says
*pre-allocate*, allocate both as `DSORG=PS, RECFM=FB, LRECL=80, BLKSIZE=3120`,
primary ~50 tracks, secondary 20 (TSO or ISPF 3.2).

Pick **one** method:

### a) FTP — an already running FTPD (no pre-allocation)

Upgrading rather than installing for the first time? Use the FTPD that is
already there (default port 2121) — it creates the datasets:

```
ftp -P 2121 your-mvs-host
> binary
> put ftpd-<version>-load.xmit    'IBMUSER.FTPD.LOAD.XMIT'
> put ftpd-<version>-samplib.xmit 'IBMUSER.FTPD.SAMP.XMIT'
> quit
```

(You can also set the attributes per upload with `quote site recfm=fb lrecl=80
blksize=3120` — FTPD honours them. You do not need it: without it a new
sequential data set already gets FB/80/3120 from the server defaults.)

### b) mvsMF (z/OSMF-compatible REST API), via zowe

If mvsMF is installed (e.g. you also run HTTPD/mvsMF). It requires the datasets
to **exist first**, so *pre-allocate*, then:

```
zowe zos-files upload file-to-data-set ftpd-<version>-load.xmit \
     "IBMUSER.FTPD.LOAD.XMIT" --binary
zowe zos-files upload file-to-data-set ftpd-<version>-samplib.xmit \
     "IBMUSER.FTPD.SAMP.XMIT" --binary
```

### c) Another FTP server (pre-allocate)

*Pre-allocate* both datasets, then transfer **binary** into them — a plain
`binary` + `put`. Any `SITE` keywords for dataset attributes vary between
MVS 3.8j TCP/IP stacks (they are **not** the z/OS syntax); pre-allocating makes
them unnecessary.

### d) IND$FILE (3270 emulator)

No pre-allocation needed. Use your emulator's file transfer in **binary** mode
(no ASCII/CRLF translation) with `RECFM=FB LRECL=80 BLKSIZE=3120`. The exact
option syntax is client-dependent — consult its file-transfer documentation.

---

## 4. Allocate the product datasets

Submit `ftpd-<version>-alloc.jcl` after you have set the two volumes. The job
allocates every library on a volume you name, never on whatever `SYSDA` picks:

| Parameter | Volume of | Default |
|---|---|---|
| `TGTVOL` | the target library `FTPD.LINKLIB` (the one SMP installs into) | `MVSRES` |
| `DLBVOL` | the distribution library `FTPD.AFTPDLOD` (the one the ACCEPT fills) | `SMP000` |

The defaults are the volumes of an MVS/CE system. **On TK5 write
`TGTVOL=TK5RES,DLBVOL=TK5DLB`**, either on the `PROC` statement or on the
`//ALLOC EXEC SMPALLOC,COND=(0,NE,CHECK)` statement at the end of the job.
Whatever your system uses, the volume must be online. If an `IEAAPF00` entry for
`FTPD.LINKLIB` exists, set `TGTVOL` to the volume it names (see
[Authorisation](#authorisation)). The space on each DD is the only other thing
worth editing.

The job has two steps. `CHECK` looks in the catalog for the two libraries. On a
first run it prints `IDC3012I ENTRY ... NOT FOUND` for both, which is the answer
wanted, and ends RC 0; then `ALLOC` creates `FTPD.LINKLIB` and `FTPD.AFTPDLOD`
and nothing else. The libraries the next step receives into are deliberately
**not** allocated here: TSO RECEIVE creates its own target and refuses to merge
into an existing dataset.

Expect `COND CODE 0000`.

**A second run does nothing.** If either library is already cataloged, `CHECK`
lists it and ends RC 8, `ALLOC` is not run (`IEF202I`, `IEF272I`) and the job ends
CC 0008. Measured on MVS/CE. Without the look, a second run would have ended RC 0
and left an uncataloged copy of the library on the other volume.

**A volume that is not mounted holds the job.** It does not fail it. Measured on
TK5 with the default `MVSRES`, which TK5 does not have: the job log shows
`IEF244I ... UNABLE TO ALLOCATE` and the console `IEF238D FTPDALC - REPLY DEVICE
NAME OR 'CANCEL'`. Reply `R nn,CANCEL` (the reply number is on the console
line); the job then ends in a JCL error. Correct the volume and submit the job
again.

> **Run this once**, on the first install. An upgrade skips it when the data
> sets are already there; on MVS/CE they are not (step 5), so MVS/CE runs it.
> If you skip it where `FTPD.LINKLIB` does not exist, the install job ends in a
> JCL error (see *Troubleshooting*). There is no DELETE step in it, on purpose. After the
> accept job, `FTPD.AFTPDLOD` holds SMP's accepted copy of the module; a
> re-run that scratched it would leave the SMP inventory reporting an install
> that is no longer on the system, and nothing would say so. To start over,
> remove FTPD first, libraries included — see
> [Removing FTPD](#11-removing-ftpd).

---

## 5. Stop a running FTPD

**Required when you are upgrading, not optional.** The APPLY writes into
`FTPD.LINKLIB`, and since 1.1.0 that is the same data set every release uses —
so it is the one a running FTPD is loading from. Replacing it underneath a live
started task risks a wild load. Stop it first.

(Before 1.1.0 each release had its own library and a running older FTPD did not
block the install. That is no longer true.)

```
/P FTPD
```

**MVS/CE ships with an FTPD 1.2.0 already running**, put there by the image and
not installed through SMP. Stop it too: a new FTPD started beside it ends with

```
FTPD002E FTPD IS ALREADY ACTIVE ON PORT 2121, THIS INSTANCE ENDS
FTPD091E INITIALIZATION FAILED RC=8
```

and only starts after `/P FTPD`.

**On MVS/CE this is a first install**, as far as SMP and the data sets go:

- The data sets `FTPD.*` do not exist (`LISTCAT LEVEL(FTPD)` finds nothing).
  The image's started task runs from `MVSLOVER.FTPD.LOADLIB`: its procedure
  has `//STEPLIB DD DISP=SHR,DSN=MVSLOVER.FTPD.LOADLIB`.
- So run the allocation job (step 4) first, then the install job (step 6).
  Skipping step 4 ends in a JCL error (see *Troubleshooting*).
- Because SMP never saw that 1.2.0, `TFTP120` is not in its inventory, so on
  MVS/CE the `APPLY` ends `COND CODE 0000`, not the `0004` described for
  upgrades in step 6.
- Replace the image's procedure with the one from `FTPD.SAMPLIB` (step 7). The
  old `MVSLOVER.FTPD.LOADLIB` stays where it is and is no longer used.

---

## 6. Install

Open `ftpd-<version>-inst.jcl` and replace the two placeholder dataset names
with what you uploaded in step 3:

```
  RECEIVE INDSN('CHANGE.ME.FTPDLOAD') -      <- IBMUSER.FTPD.LOAD.XMIT
  RECEIVE INDSN('CHANGE.ME.SAMPLIB') -       <- IBMUSER.FTPD.SAMP.XMIT
```

Those are the only lines you have to change on most systems. Submit it.

**On MVS/CE, name the volume.** TSO RECEIVE allocates `FTPD.FTPDLOAD` and
`FTPD.SAMPLIB` itself and lets the system pick the volume. Put the two data
sets on the public pack explicitly: in both `RECEIVE` commands of the job, take
the comment marks off the `VOLUME` line and name the volume, `PUB000` on a stock
MVS/CE:

```
  RECEIVE INDSN('IBMUSER.FTPD.LOAD.XMIT') -
    VOLUME('PUB000') -
    DATASET('FTPD.FTPDLOAD')
  RECEIVE INDSN('IBMUSER.FTPD.SAMP.XMIT') -
    VOLUME('PUB000') -
    DATASET('FTPD.SAMPLIB')
```

The job's own header says that steps 3 and 4 there (the names, and this volume)
are the only edits it needs; on TK5 leave the `VOLUME` lines alone. Measured
on MVS/CE with this edit: `FTPD.FTPDLOAD` and `FTPD.SAMPLIB` on `PUB000`,
`FTPD.LINKLIB` on `MVSRES` and `FTPD.AFTPDLOD` on `SMP000` (the defaults of the
allocation job). `FTPD.SAMPLIB` is the one you keep, so check it with ISPF 3.4
after the job and read the volume from the listing. Without the `VOLUME` lines the
job was not run on MVS/CE with 1.3.4.

**The job runs six steps, each conditional on the ones before, so it stops at
the first failure rather than building on it, and then it ends.** The accept job
is a job of its own:

| Step | What it does |
|------|--------------|
| `DELOLD` | scratches the RECEIVE targets, so the job can be re-run |
| `RECV1` | load XMIT → `FTPD.FTPDLOAD` (a staging library) |
| `RECV2` | samplib XMIT → `FTPD.SAMPLIB` |
| `RECV` | receives the SYSMOD into the SMP inventory |
| `APPLYCHK` | dry run — `APPLY` only proceeds if this ends RC 0 |
| `APPLY` | copies the load module into `FTPD.LINKLIB` |

The SYSMOD travels inline in the job — there is no third file to upload.

At the end of the job a comment says the function is applied, not accepted, and
names `ftpd-<version>-accept.jcl` as the next job. **Do not submit it yet.** Run
the steps 7 to 9, try the product, then submit it (step 9a).

**What a good run looks like.** On a fresh install, every step `COND CODE
0000`, and in the SMP output:

```
HMA3930    SYSMOD TFTP134 SUCCESSFULLY RECEIVED
HMA2380    COPY SUCCESSFUL - MOD=FTPD - LMOD=FTPD - LIBRARY=LINKLIB - SYSMOD=TFTP134 - RETURN CODE=00
HMA2050    APPLY PROCESSING COMPLETED - HIGHEST RETURN CODE IS 00
```

These lines were measured with the published package of 1.3.4, installed the way
this guide describes (the allocation job, then the install job), on TK5 and on
MVS/CE. All six steps of the install job ended with `COND CODE 0000`.

**On an upgrade the `APPLY` ends `COND CODE 0004` instead, and that is
correct** — `HMA2461` reporting that the deleted level has no backup entry.
(Not on MVS/CE, a first install, where the `APPLY` ends `0000` — see step 5.)
See *One return
code that looks wrong and is not* in section 2b. `HMA2270 APPLY PROCESSING
SUCCESSFULLY COMPLETED` is the line that says the `APPLY` worked; the return
code is not.

`HMA2380` is the message that separates a real install from one that copied
nothing. **If it is not in the job log, nothing was installed** — whatever the
condition codes say.

Then check `FTPD.LINKLIB` really holds `FTPD` (ISPF 3.4). Do look: SMP reports
the library by **ddname**, and a ddname says nothing about which dataset was
behind it. That is not a nicety — it is exactly why an upgrade leaves the old
library populated, and section 2b is about the consequences.

SMP **copies** this module rather than re-binding it, which is why the `AC(1)`
authorisation code and the link attributes are exactly what the build produced.

---

## 7. Install the procedure and the configuration

SMP does not touch your PROCLIB or PARMLIB, and that is deliberate: **the
product owns the patterns, your system owns the copies.** If SMP owned the
running procedure, every change you made to it would be silently replaced by
the next update. So this step is yours, and it is the one place where you have
to read what you are copying.

Copy from `FTPD.SAMPLIB`:

| Member | Copy to | Adjust |
|--------|---------|--------|
| `FTPD` | a PROCLIB in the started-task concatenation | no — `STEPLIB` already names this release's LINKLIB, and JES mode needs no DDs (see below) |
| `FTPDPRM0` | a PARMLIB | **yes — see below** |

`SYS2.PROCLIB` is the usual home for the procedure. **On MVS/CE it already
holds a member `FTPD`**, the procedure of the image's FTPD 1.2.0, whose
`STEPLIB` is `MVSLOVER.FTPD.LOADLIB`. Replace it with the member from
`FTPD.SAMPLIB`, whose `STEPLIB` names `FTPD.LINKLIB`; otherwise the started
task keeps running the old module.

### Which PARMLIB

The shipped procedure defaults to `D='SYS2.PARMLIB'`. **On TK5 that dataset
does not exist**, and the `SYS1.PARMLIB` of TK5 has no room for another
member. A plain copy into it ends like this:

```
IEC031I D37-04,IFG0554T,PARMCPY,COPY,SYSUT2,390,TK5RES,SYS1.PARMLIB
IEF450I PARMCPY COPY - ABEND SD37 U0000
```

`SD37` is the system's out-of-space abend: the member did not fit. There are
two ways out. Use one of them, not both.

**a) Compress `SYS1.PARMLIB`, then copy.** The compress gives back the space
that replaced members left behind. It needs the data set exclusively
(`DISP=OLD`), so nobody may be using it at the time:

```
//PARMCMP  JOB (ACCT),'COMPRESS PARMLIB',CLASS=A,MSGCLASS=H
//COMPRESS EXEC PGM=IEBCOPY
//SYSPRINT DD SYSOUT=*
//SYSUT3   DD UNIT=SYSDA,SPACE=(TRK,(10,10))
//SYSUT4   DD UNIT=SYSDA,SPACE=(TRK,(10,10))
//PARMLIB  DD DSN=SYS1.PARMLIB,DISP=OLD
//SYSIN    DD *
  COPY INDD=PARMLIB,OUTDD=PARMLIB
/*
```

It ends `COND CODE 0000`, with `IEB161I COMPRESS TO BE DONE USING INDD NAMED
PARMLIB` in `SYSPRINT`. Then run the copy of `FTPDPRM0` again, from
`FTPD.SAMPLIB(FTPDPRM0)` to `SYS1.PARMLIB(FTPDPRM0)`; on a TK5 that had run
into `D37-04` it then ended `0000`. Start FTPD with
`/S FTPD,D='SYS1.PARMLIB'`, or edit `D=` in your copy of the procedure.

**b) Use a PARMLIB of your own and leave `SYS1.PARMLIB` alone.** Allocate a
partitioned data set, copy the member into it, and name it with `D=`:

```
//PRIVPL   JOB (ACCT),'PRIVATE PARMLIB',CLASS=A,MSGCLASS=H
//ALLOC    EXEC PGM=IEFBR14
//PARMLIB  DD DSN=HERC01.PARMLIB,DISP=(NEW,CATLG),UNIT=SYSDA,
//            SPACE=(TRK,(5,5,10)),DCB=(RECFM=FB,LRECL=80,BLKSIZE=3120)
//COPY     EXEC PGM=IEBGENER
//SYSPRINT DD SYSOUT=*
//SYSIN    DD DUMMY
//SYSUT1   DD DSN=FTPD.SAMPLIB(FTPDPRM0),DISP=SHR
//SYSUT2   DD DSN=HERC01.PARMLIB(FTPDPRM0),DISP=SHR
```

```
/S FTPD,D='HERC01.PARMLIB'
```

Use your own high-level qualifier for the data set, and put the same `D=` into
your copy of the procedure if you want the plain `/S FTPD` to find it. An `IEBCOPY` from `SYS1.PARMLIB` into a library with
a larger block size failed on the test system (`IEB139I`, `IEB100I`, return
code 8), which is why the job copies with `IEBGENER`.

On MVS/CE, `SYS2.PARMLIB` exists and the default is fine. **It already holds
a member `FTPDPRM0`**, the settings of the image's FTPD 1.2.0. Replace it with
the member from `FTPD.SAMPLIB` (the `IEBGENER` step above, with
`SYS2.PARMLIB(FTPDPRM0)` as `SYSUT2`). If it stays, the new FTPD starts and
reads the old settings. In the image measured, the old member sets
`PASVADR=127,0,0,1`, `DEFUNIT=3390` and `DEFVOLUME=PUB001`, where the shipped
member has `PASVADR=ANY` and leaves unit and volume to the system, and the
start ends with `FTPD062W 1 SETTING(S) IN DD:FTPDPRM HAVE NO EFFECT AND WERE
IGNORED` for `JESINTERFACELEVEL`, which FTPD no longer reads. `/F FTPD,CONFIG`
shows the settings that are in effect. `M=` selects the
member (default `FTPDPRM0`), so a second configuration can live alongside the
first.

### JES mode needs no DDs

`SITE FILETYPE=JES` (job submission and spool retrieval) reads the JES2
checkpoint and the spool. With the libc370 this release is built against, FTPD finds both itself: the checkpoint
through the catalog (or, if it is not cataloged, by looking at the online
volumes), the spool from the volume the checkpoint names. The shipped
procedure therefore has **no `HASPCKPT`/`HASPACE1` DDs**, and nothing here has
to be edited for a standard JES2.

Names come from JES2's `DSNPRFX`, `SYS1` unless your site changed it
(`SYS1.HASPCKPT`, `SYS1.HASPACE`). Add the two DDs to the procedure only when

- your JES2 uses another prefix, or
- the checkpoint is neither cataloged nor on an online volume.

```
//HASPCKPT DD  DISP=SHR,DSN=SYS1.HASPCKPT,UNIT=unit,VOL=SER=volser
//HASPACE1 DD  DISP=SHR,DSN=SYS1.HASPACE,UNIT=unit,VOL=SER=volser
```

Each DD overrides the lookup for its own data set (`HASPCKPT` the checkpoint,
`HASPACE1` the spool), so you add only the one that needs it. Use the unit and
volume of **your** system, with `DSN` as JES2 names the data set. Procedures
from earlier releases carry both DDs with development-system values
(`UNIT=3350`, `VOL=SER=MVS000` / `SPOOL1`); on any other system delete them or
correct them. A DD with the wrong `DSN` or volume makes the JES commands fail
with `451`; a DD that merely omits `UNIT`/`VOL` works when the data set is
cataloged.

### Editing FTPDPRM0

The shipped member is a working default, but three settings are worth a look
before the first start:

```
SRVPORT=2121
PASVPORTS=22000-22200
MAXSESSIONS=10
IDLETIMEOUT=300
DEFPRIMARY=10
DEFSECONDARY=5
DEFSPACETYPE=TRACK
```

- `SRVPORT` is 2121, not 21 — nothing else on the system has to move out of the
  way, and an unprivileged client reaches it with `ftp -P 2121`.
- `PASVPORTS` must be a range your firewall or NAT lets through, or passive
  mode stalls after `227`.
- `PASVADR` is `ANY`, which is right when the client reaches the host
  directly. Behind NAT, port forwarding or a container network, set it to the
  address the client can reach, written as an IPv4 address, not as a name, and
  forward the whole `PASVPORTS` range to the host. Left at `ANY`, FTPD gives
  the client a private address, and some clients wait 30 seconds for it before
  every transfer (see Troubleshooting).
- `MAXSESSIONS` is the most sessions at the same time. The next connection is
  closed at once, without a reply, and FTPD logs `max sessions reached,
  rejecting` to `STDOUT`. FTPD runs one worker thread for each session, so
  every value from 1 up works.
- `DEFUNIT`/`DEFVOLUME` are where FTPD allocates a dataset a client creates
  without saying where. **Both are unset by default, which means the system
  chooses** — set them to keep uploads off a system pack. `DEFVOLUME` is checked
  once at startup against the volumes that are online: one that is not there is
  reported as `FTPD059W` and ignored rather than failing every upload, and a
  `DEFUNIT` that disagrees with that volume's device type is reported as
  `FTPD060W` and dropped. A client overrides both per session with `SITE VOLUME=`
  and `SITE UNIT=`, and those are deliberately *not* checked — a client that
  names a volume itself gets a failed allocation if it is wrong, because it is
  there to read the reply.
  With both unset, MVS/CE chooses `WORK00` or `WORK01`, which are 3350
  volumes (`PUB000` is a 3380, `PUB001` a 3390). A 3350 track holds less than a
  3390 track, so a data set that a client creates with a small `SITE PRIMARY`
  in tracks is full after fewer records there; set `DEFVOLUME` (and `DEFUNIT`)
  when the result must not depend on the device.

  Note for anyone upgrading from 1.0.2 or earlier: these two keys were stored
  and never used, so whatever your `FTPDPRM` says about them has had no effect
  until now. Check the value before relying on it — the sample shipped
  `PUB001` for years while nothing read it.
- `DEFPRIMARY`/`DEFSECONDARY`/`DEFSPACETYPE` are how much space that dataset
  gets. **The shipped `TRACK 10,5` caps an upload at 85 tracks** — 10 tracks
  plus at most 15 secondary extents of 5, roughly 4 MB at FB/80/3120 — and a
  larger upload ends in an x37 ABEND. FTP tells the server no size at `STOR`
  time, so the allocation cannot come from the transfer; these numbers are all
  there is. On a system that receives large files set
  `DEFSPACETYPE=CYLINDER`, `DEFPRIMARY=10`, `DEFSECONDARY=10` for about 2400
  tracks. The 16-extent limit and the single volume stay either way, so a larger
  `DEFPRIMARY` buys more than a larger `DEFSECONDARY`. A client can override all
  three per session with `SITE PRIMARY=`, `SECONDARY=`, `TRACKS`, `CYLINDERS`
  or `BLOCKS`.
- `DEFDIRECTORY` is the number of directory blocks of a partitioned data set
  that a client creates with `MKD`: 10 unless you set it, 1 to 16777215. It
  replaces the fixed 10 of earlier releases; the PDS directory cannot grow
  afterwards, so raise it where clients create PDSs that will hold many
  members. A client overrides it per session with `SITE DIRECTORY=`. `/F
  FTPD,CONFIG` shows it at the end of the `FTPD058I` line, as
  `DEFDIRECTORY=n`.
- `SSLPROXY=YES` is only for a TLS-terminating proxy in front of FTPD. Run bare
  it promises clients a confidentiality it does not deliver; the member's own
  comments spell this out.
- `BINDTRIES`/`BINDWAIT` (both 1–100, defaulting to 3 and 5) decide how long FTPD
  keeps trying a listener port it cannot have yet — a port still held, or a
  stack that is not up. Giving up ends the started task, so raise `BINDTRIES`
  if `/S FTPD` runs early in an IPL.
- `MAXRETPD` is the most days a client may ask for with `SITE RETPD`: 365 unless
  you set it, 0 to 9999. FTPD does not store into a data set with an unexpired
  retention period (`STOR` and `APPE` are answered `550`), so the limit caps
  how long a client can keep a name blocked that way. `SITE RETPD=n` with `n`
  above the limit is answered
  `501 RETPD=n exceeds the server limit of N days; keeping the current value`
  and the session keeps its retention period. `MAXRETPD=0` allows no retention
  at all. A value that is not a whole number from 0 to 9999 is ignored with a
  warning in the log (`MAXRETPD must be 0..9999, keeping N`) and the previous
  value stays. A `FTPDPRM` from an earlier release needs no change: without the
  key the limit is 365, and `SITE RETPD` itself is new in 1.3.0. The shipped
  `FTPDPRM0` carries the key. `/F FTPD,CONFIG` shows the value in use as
  `FTPD061I   MAXRETPD=n`.

A `#` in the first non-blank column comments a line out — it has to be first,
because a `#` anywhere else is data.

---

## 8. Set up RAKF

Without this, FTPD starts and refuses every login. The short version:

1. Give the started task `FTPD` a low-privilege identity in RAKF. FTPD does
   not log on to one itself: it keeps the identity RAKF assigned when the task
   was created. On a RAKF that assigns an identity by STC name, map the name
   `FTPD` to a userid with Operations authority `N`; on any other RAKF the task
   runs as `STC`/`STCGROUP`, which has Operations authority. `FTPD004I` at
   startup names the identity you got — see step 9.

   **RAKF 2.0.0 maps by STC name:** the mapping is a userid `FTPD` in
   `SYS1.SECURE.CNTL(USERS)`, and its group has to be allowed `READ` on the
   `FACILITY` profile `SVC244`, or FTPD ends in `FTPD003W APF SETUP FAILED`
   (FTPD asks for its authorization with SVC 244). Use a group that holds
   nothing else, such as `STARTED` in the `PROFILES` that come with RAKF 2.0.0
   — not `STCGROUP`, which has ALTER on all datasets there. Create the user
   with `ADDUSER FTPD PASSWORD(pw) DFLTGRP(STARTED)`; `ADDUSER` and `PERMIT`
   need the SPECIAL authority, which is column 30 of the `USERS` line (a `Y`),
   and a `USERS` table from an earlier RAKF has none. The user must also be
   connected to the group `RAKFADM`. The setup guide, sections 1 and 2, has the
   details.

2. Define the `FTPAUTH` FACILITY profile, which controls who may use FTP at
   all, and let the group of your FTP users read it. If it is **not** defined,
   RAKF answers "undefined resource" with ALTER and every authenticated user is
   let in.

   **Do not edit `PROFILES` by hand; RAKF's maintainer recommends the
   commands.** Create profiles with `RDEFINE`, `PERMIT` and `ADDSD`, then
   reload with `S RAKFPROF`. The commands keep the file sorted, refuse a group
   line that has no universal line, and do not write a profile twice. A
   hand-edited file that is wrong aborts the reload (`U0300`, `U0400`,
   `U0500`) and, on RAKF 2.0.0, keeps JES2 from starting at the next IPL.

   Run the commands in TSO under a userid with the SPECIAL authority that is
   connected to the group `RAKFADM`, each command starting in column 1 (`USER`
   for every ordinary user, or your own group):

   ```
   RDEFINE FTPAUTH CLASS(FACILITY) UACC(NONE)
   PERMIT FTPAUTH CLASS(FACILITY) ID(USER) ACCESS(READ)
   ```

   You need not look first whether `FTPAUTH` is already there. MVS/CE ships
   it, with the universal entry `NONE` and the group `ADMIN` at `READ`; there
   `RDEFINE` answers `RDEFINE: Profile already defined` and changes nothing,
   and `PERMIT` adds the line for your group. A RAKF 1.2.x has no such
   commands; the reference in section 7 of the RAKF guide covers the file
   there.

3. Reload the tables: `S RAKF` reloads the users and the profiles and
   answers `RAKFPROF7  RAKF PROFILES UPDATED` and `RAKFUIDS4  USER TABLE
   UPDATED`; `S RAKFPROF` reloads only the profiles. The lines the commands
   wrote are described in section 3 of the RAKF guide.

**One security invariant:** the identity of the started task must stay
least-privilege — no broad `ID(FTPD) ACCESS(ALTER)` dataset profiles. Per-user
access is authorised against the *logged-in user's* identity, and the ABEND
recovery path can transiently put a concurrent session on the started task's
ACEE. While that identity is minimally privileged the switch can only lose
authority; give it broad authority and the same switch becomes fail-open.
`STC`/`STCGROUP` is such an identity.

**Upgrading from 1.2.0 or earlier:** those releases logged on as `FTPD`/`USER`
at startup, so the started task ran under that identity whatever RAKF assigned.
FTPD 1.3.0 and later do not. On a system whose RAKF has no mapping by STC name, such as
TK5 before Update 6, the started task now runs as `STC`/`STCGROUP` again, with
Operations authority, and the `FTPD` line in `USERS` is no longer used by FTPD.
On RAKF 2.0.0 that line is the mapping, and its group must be allowed SVC 244
(above). The group `USER` of the FTPD 1.2.0 guide is not one that the shipped
`SVC244` profile allows.

The complete reference — dataset access levels per FTP command, per-user HLQ
patterns, RAKFCL equivalents and the reasoning behind the invariant — is
[FTPD_RAKF_SETUP.md](https://github.com/mvslovers/ftpd/blob/v1.3.4/docs/FTPD_RAKF_SETUP.md).

---

## 9. Start and verify

```
/S FTPD                      default member (FTPDPRM0)
/S FTPD,M=FTPDPRM1           alternate config member
/S FTPD,D='MY.PARMLIB'        alternate PARMLIB -- TK5, see step 7
```

On MVS/CE, stop the FTPD 1.2.0 that comes with the image first (`/P FTPD`,
step 5); otherwise this start ends in `FTPD002E`/`FTPD091E`.

A healthy start ends in `FTPD001I … READY`:

```
FTPD000I FTPD 1.3.4 (B079CED) STARTING
FTPD005I LIBC370 2.29.2 (70E450F)
FTPD008I AUTHORIZED BY SVC (MODULE KEY 8)
FTPD004I STC RUNS AS <user>/<group>
RAKF0004 INVALID ATTEMPT TO ACCESS SYSTEM:  USER:?FTPDPRBO
FTPD054I LISTENING ON ANY PORT 2121
FTPD001I FTPD 1.3.4 READY
```

These lines were measured with the published package of 1.3.4 on MVS/CE, with FTPD
started from the library of the install job, before the accept job. There the
`FTPD004I` line read `FTPD/STARTED` and the `RAKF0004` line carried one more
character behind the name, as explained below. A start on TK5 was not run
with the published package. The version and the hash in the first line, and
the libc370 level and hash in the second, are those of the release you installed: a
later release prints its own. The identity in the fourth line is the one RAKF gave
your started task. If your parameter
file still has settings that no longer have an effect, `FTPD062W` appears before
`FTPD054I`.

`FTPD004I` names the identity RAKF gave the started task: the one you mapped
the name `FTPD` to, or `STC`/`STCGROUP` where RAKF has no mapping (step 8).

**The `RAKF0004` line is expected, and it is not an error.** It does not come
from FTPD but from RAKF. Since FTPD 1.3.3 an authorized FTPD checks once at
every start that RAKF really verifies a user ID: it tries to log in the user ID
`?FTPDPRB` with a password that cannot be valid. The `?` is not a character of
a user ID, so RAKF must refuse the login, and it reports the refusal with this
line. The refusal is the answer FTPD expects. RAKF may print one more character
behind the eight characters of the name (`?FTPDPRBO` above), so a rule that
looks for the line has to match the prefix `?FTPDPRB`. An automation that
reacts to `RAKF0004` has to leave this user ID out. An FTPD that is not
authorized (`FTPD003W`) makes no check and leaves no `RAKF0004` line.

**`FTPD004E` means that the check failed.** If RAKF accepts the login of
`?FTPDPRB`, it accepts any user ID and password, and FTPD does not trust it. It
writes two lines to the console, instead of the `RAKF0004` line:

```
FTPD004E THE SECURITY SYSTEM ACCEPTS ANY USERID AND PASSWORD
FTPD004E ALL LOGINS ARE REFUSED: CORRECT THE SECURITY TABLES, THEN RESTART FTPD
```

FTPD goes on and ends the start with `FTPD001I … READY`, but every `PASS` is
answered with `530 Login not possible: the security check of this server is not
working. Ask the administrator to check the FTPD004E console message.` In the
measured case RAKF reported its tables as invalid or missing: it wrote
`RAKF0001 USERID TABLE INVALID OR MISSING` and
`RAKF0002 STC ACCESS ALLOWED: USER: ?FTPDPRBO` for the check user ID. Correct
the tables (step 8, and
[FTPD_RAKF_SETUP.md](https://github.com/mvslovers/ftpd/blob/v1.3.4/docs/FTPD_RAKF_SETUP.md),
section 6), then stop and start FTPD (`/P FTPD`, `/S FTPD`). The check is made
only at the start. Correcting the tables alone changes nothing, and no console
command repeats the check.

The two build stamps identify exactly what is running: `FTPD000I` gives the
version and the commit it was built from, `FTPD005I` the libc370 it was linked
against — quote both in a bug report. A build made from a modified working tree
marks its hash `-DIRTY` and adds `FTPD006W`; a released build never does.

`FTPD008I` says which of the two authorisation routes the start took, and
reads `AUTHORIZED BY LIBRARY (MODULE KEY 0)` on a system with the LINKLIB in
the APF list — see step 2. Quote it in a bug report too: it decides the storage
key FTPD's own module runs in.

Then log in from a client:

```
ftp -P 2121 your-mvs-host
```

Operator commands:

```
/F FTPD,STATS                status, session counts, byte counters, trace state
/F FTPD,SESSIONS             active client sessions
/F FTPD,CONFIG               the configuration actually in effect
/F FTPD,VERSION              build stamps again
/F FTPD,TRACE ON|OFF|DUMP    protocol trace ring buffer
/F FTPD,HELP                 every MODIFY command
/P FTPD                      orderly stop (FTPD099I SHUTDOWN COMPLETE)
```

`/F FTPD,CONFIG` is worth one look after the first start: it prints what FTPD
parsed, not what the member says, so a typo that fell back to a default is
visible there and nowhere else.

---

## 9a. Accept

Submit `ftpd-<version>-accept.jcl` (job `FTPDACC`) when you have tried the
product and want to keep this level. Nothing in it needs editing. It has four
steps and ends CC 0000 when the function is applied:

| Step | What it does |
|------|--------------|
| `GUARD` | `LIST CDS SYSMOD(TFTP134)`: is the function applied? RC 0 if it is, RC 4 if it is not |
| `NOTAPPL` | runs only when `GUARD` did not end RC 0, and ends the job with RC 8 |
| `ACCEPT` | `ACCEPT S(TFTP134)`: copies the module into `FTPD.AFTPDLOD`, the base a later `RESTORE` of a PTF returns to |
| `CLEANUP` | scratches the staging library `FTPD.FTPDLOAD`, which is now spent |

**This is the point without a way back.** Once `TFTP134` is accepted, SMP refuses
`RESTORE` and `REJECT` for it (section 11). Until then, a first install can still
be taken out again with a `RESTORE`; an upgrade cannot (section 2a).

What the job leaves behind, and what it prints in the failure cases, is in
section 1. The messages to look for:

```
HMA2380    COPY SUCCESSFUL - MOD=FTPD - LMOD=FTPD - LIBRARY=AFTPDLOD - SYSMOD=TFTP134 - RETURN CODE=00
HMA2270    ACCEPT PROCESSING SUCCESSFULLY COMPLETED FOR SYSMOD TFTP134
IDC0550I ENTRY (A) FTPD.FTPDLOAD DELETED
```

(measured with the published package of 1.3.4; after an upgrade the `ACCEPT` also
prints `HMA2240 SUCCESSFULLY DELETED MODULE FTPD ON AFTPDLOD LIBRARY`, the module
of 1.3.3 that the new one replaces.) Then check the three things at the end of
section 1: the member `FTPD` in `FTPD.AFTPDLOD`, no `FTPD.FTPDLOAD`, and `REC APP
ACC` for `TFTP134` in both zones.

If the `ACCEPT` fails, `CLEANUP` does not run and nothing is scratched: fix the
cause and submit the job again. (That case was not run.) If you decide not to
accept — you took the function out with a `RESTORE`, or you keep it applied —
scratch the staging library yourself:

```
//SCRATCH EXEC PGM=IDCAMS
//SYSPRINT DD  SYSOUT=*
//SYSIN    DD  *
  DELETE FTPD.FTPDLOAD NONVSAM SCRATCH PURGE
/*
```

The install job needs no such scratch before a second run: its `DELOLD` step
deletes `FTPD.FTPDLOAD` and `FTPD.SAMPLIB` itself.

---

## 10. What FTPD can reach

Three namespaces, selected per session:

- **MVS datasets** — the default. Sequential datasets and PDS members, with
  z/OS-compatible `SITE` keywords for `RECFM`, `LRECL`, `BLKSIZE`, `SPACE`,
  `UNIT` and `VOLUME`.
- **JES** — `quote site filetype=jes`; submit a job, list your jobs, retrieve
  spool output. Needs nothing in the procedure: see *JES mode needs no DDs* in step 7.
- **UFS** — the UFSD filesystem, reached by `cd` to a UFS path. Needs the UFSD
  started task to be *running*, not just installed; without it those commands
  answer `550 UFS service not available`.

The command set and the `SITE` keywords are documented in
[ZOS_FTP_REFERENCE.md](https://github.com/mvslovers/ftpd/blob/v1.3.4/internals/ZOS_FTP_REFERENCE.md);
the architecture is in
[FTPD_CONCEPT.md](https://github.com/mvslovers/ftpd/blob/v1.3.4/internals/FTPD_CONCEPT.md).

---

## 11. Removing FTPD

Because the installation is SMP-managed, there is a defined way back — but it
is **not** the `RESTORE` followed by `REJECT` that SMP documentation leads you
to expect. The accept job accepts the FMID (step 9a), and
an accepted function SYSMOD refuses both: `RESTORE` because it was accepted,
and `REJECT` because the SMPPTS no longer holds the SYSMOD (`PURGE=YES` in its
system entry on the system measured). Between the install job and the accept job
the function is only applied: a `RESTORE` then removes it on a first install, and
is refused after an upgrade (section 2a). The removal guide has the measured
states of both.

The way that works is a dummy function, `TFTPDEL`, which deletes every release
of FTPD through `++VER DELETE` and takes the load module out of both libraries
with it. A short `UCLIN` job then frees the ids, and a `LIST` proves it. A way
that uses `UCLIN` alone stays as the fallback.

The full procedure, with the jobs to submit and the messages to check, is in
[uninstall.md](https://github.com/mvslovers/ftpd/blob/v1.3.4/docs/uninstall.md).

**This is for removing FTPD, not for upgrading it.** An upgrade installs over
the release before it — see sections 2a and 2b — and needs none of this.

What SMP does **not** remove, because it never owned them: the copies you made
in step 7, and your RAKF definitions. Those are yours to delete.

---

## Troubleshooting

| Symptom | Likely cause |
|---------|--------------|
| Allocation job fails, dataset already exists | It was already run, or `FTPD.*` exists from an SMP install of 1.2.0. Do not force it — see the warning in step 4 |
| The install job ends in a JCL error at `APPLYCHK`: `IEF212I ... APPLYCHK LINKLIB - DATA SET NOT FOUND` | `FTPD.LINKLIB` does not exist: the allocation job (step 4) was skipped. This is the MVS/CE case: there the image's FTPD is not an SMP install and `FTPD.*` is not there. Run the allocation job, then resubmit the install job — and read the next row |
| After the allocation job, the resubmitted install job ends `COND CODE 0008` in `RECV`: `HMA3862 ** SYSMOD TFTP134 ALREADY RECEIVED` | The first run had already received the SYSMOD before it stopped at `APPLYCHK`. The rerun ends RC 8 in `RECV` (`HMA3862`, `HMA3902`, `HMA3920`, `HMA2481`); `APPLYCHK` and `APPLY` are not run (`IEF202I ... NOT RUN BECAUSE OF CONDITION CODES`, `IEF272I`), and **nothing is installed**. There is no `ACCEPT` or `CLEANUP` in this job. Take the received SYSMOD out first: `REJECT S(TFTP134)` (RC 0, `HMA2270`), then submit the install job again; it ends CC 0000 (measured on TK5 and on MVS/CE with the published package of 1.3.4, the first run ending in the JCL error at `APPLYCHK` described in the row above). The same `HMA3862` ends a second run of the install job when `TFTP134` is already applied: CC 0008, nothing changed. The way out there is a `RESTORE` (first install only, section 2a) or the removal guide |
| The accept job ends in a JCL error: `IEF212I ... FTPDLOAD - DATA SET NOT FOUND` | The staging library is not there: a first accept job scratched it. `GUARD` ended RC 0 and no step ran, so nothing changed (step 9a). Check with `LIST CDS SYSMOD(TFTP134)` that `REC APP ACC` is there |
| The accept job ends `COND CODE 0008` in `NOTAPPL` | `GUARD` did not find `TFTP134` in the target zone (it ended RC 4): the install job never ran, ended RC 8 or 12 before the `APPLY`, or the function was taken out again. `ACCEPT` and `CLEANUP` did not run; nothing is accepted or scratched. Measured after a removal |
| `IEC031I D37-04 ... SYS1.PARMLIB` (`ABEND SD37`) while copying `FTPDPRM0` — TK5 | `SYS1.PARMLIB` is full. Compress it or use a PARMLIB of your own — step 7 |
| `RECV1`/`RECV2` fails, target exists | Something else allocated it. RECEIVE refuses to merge; scratch it and re-run |
| `APPLYCHK` ends non-zero, `APPLY` skipped | Read the SMP output — the check exists to stop before anything is written. A missing DD is the usual cause |
| SMP reports success, but the module is not where you expected | A ddname says nothing about the dataset behind it. Check the JCL, then look at the library itself |
| `S806` (module not found) at `/S FTPD` | `STEPLIB` in the procedure does not name the LINKLIB the APPLY wrote to |
| `/S FTPD` rejected — procedure not found | Procedure not copied into a PROCLIB in the started-task concatenation |
| `FTPD003W APF SETUP FAILED`; every `PASS` answers `530 Login not possible: FTPD is not APF authorized` | No RAKF (so no SVC 244) and no APF entry — step 2. Nobody can log in until this is fixed |
| `FTPD004I STC RUNS AS STC/STCGROUP` | RAKF assigned the default started-task account, which has Operations authority — step 8 |
| `FTPD004W NO SECURITY ENVIRONMENT FOR THE STC` | The address space has no ACEE, and RAKF permits any access without one — step 8 |
| `RAKF0004 INVALID ATTEMPT TO ACCESS SYSTEM:  USER:?FTPDPRB…` right after `FTPD004I` | Not an error. It is the refusal of the check login that FTPD makes at every start — step 9 |
| `FTPD004E THE SECURITY SYSTEM ACCEPTS ANY USERID AND PASSWORD`; every `PASS` answers `530 Login not possible: the security check of this server is not working` | RAKF accepted the check login, so its tables do not work (not loaded, or invalid). Correct them (step 8), then stop and start FTPD: the check is made only at the start — step 9 |
| `FTPD002E FTPD IS ALREADY ACTIVE ON PORT n` | An older instance is still up. `/P FTPD` first |
| `FTPD009E PARMLIB (DD:FTPDPRM) READ FAILED` | An I/O error on the parmlib data set, not a syntax error. FTPD refuses to start on a half-read configuration -- the member is unreadable where it stopped, so copy it in again |
| `530 Login incorrect` for a valid user | RAKF user or password wrong — step 8 |
| `530 Not authorized for FTP access` | The `FTPAUTH` FACILITY profile denies this user — step 8 |
| `550 Access denied to <dsn>` | A RAKF `DATASET` profile denies it, checked under the client's identity |
| `451` on every JES command | A `HASPCKPT`/`HASPACE1` DD in the procedure names the wrong data set or volume — delete it or correct it, step 7 |
| `451 Read error on data set` / `451 Write error on data set` after a partial transfer | An uncorrectable I/O error on the data set itself. The transfer is incomplete and reported as such rather than as a success — the byte count in the reply says how far it got |
| `552 ... out of space on the data set` on an upload | The data set could not grow: the space it was allocated is used up. `552` is permanent on purpose — retrying the same upload into the same allocation cannot succeed. Give the transfer more space *before* the next attempt: raise `DEFPRIMARY`/`DEFSECONDARY`/`DEFSPACETYPE` in `FTPDPRM`, or have the client send `SITE CYLINDERS PRIMARY=10 SECONDARY=10`. If FTPD created the data set for this upload it scratches it again, so the retry starts from a clean allocation. |
| `550 UFS service not available` | The UFSD started task is not running (installed is not enough) |
| Passive transfer stalls after `227` | `PASVPORTS` not reachable, or `PASVADR` wrong behind NAT — step 7 |
| Every transfer waits about 30 seconds with some clients (the Zowe FTP plugin), not with others (curl, Python `ftplib`) | Behind NAT, port forwarding or a container network, `PASVADR=ANY` puts the private local address of the control connection into the `227` reply. The Zowe FTP plugin 3.1.1 connects to it, waits 30 seconds, then tries the control address; curl and `ftplib` connect to the control address at once. Set `PASVADR` to the address the client can reach, forward the whole `PASVPORTS` range, and start FTPD again — step 7 |
| `S106` at start on a freshly installed library | The XMIT was uploaded in text mode. Re-upload in **binary** and re-run the install job |
| `S0C4` at start, JESMSGLG holds only the `IEF450I` — no `FTPD000I`, no banner | FTPD 1.0.0 started from an **APF-authorised** LINKLIB. The module is then key 0 and the first store into its own storage abends before the first WTO. Upgrade to 1.0.1, or take the APF entry back out and let SVC 244 authorise instead — step 2 |

For the RAKF message reference, see
[FTPD_RAKF_SETUP.md](https://github.com/mvslovers/ftpd/blob/v1.3.4/docs/FTPD_RAKF_SETUP.md).
