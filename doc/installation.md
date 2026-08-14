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

Two placeholders are used throughout:

| | |
|---|---|
| `<version>` | the release, e.g. `1.0.0` — it appears in every shipped file name |
| `<vrm>` | the same release as MVS dataset qualifier, e.g. `V1R0M0` |

Both are already filled in inside the shipped jobs; you only need them to
recognise which file is which.

---

## 1. What is in the archive

| File | What it is |
|------|------------|
| `README.md` | this guide |
| `ftpd-<version>-load.xmit` | the FTPD load module, as a TSO RECEIVE-ready XMIT |
| `ftpd-<version>-samplib.xmit` | the sample library: STC procedure and configuration pattern |
| `ftpd-<version>-alloc.jcl` | allocates the datasets SMP installs into — **run once** |
| `ftpd-<version>-inst.jcl` | receives everything and installs it — repeatable |

FTPD is a single load module. The sample library holds two members, `FTPD`
(the started task procedure) and `FTPDPRM0` (the configuration pattern).

Where everything ends up:

```
FTPD.<vrm>.LINKLIB     the load module -- point STEPLIB here
FTPD.<vrm>.SAMPLIB     the patterns you copy from in step 7
FTPD.<vrm>.AFTPDLOD    SMP's distribution library, the base a RESTORE returns to
```

---

## 2. Prerequisites

- **MVS 3.8j** on Hercules, up and IPLed, with SMP 4 usable (the `SMPREC` and
  `SMPAPP` procedures and the `SYS1.SMP*` datasets — present on TK4-/TK5 and
  MVS/CE as shipped).
- **RAKF**, for authentication — see below. This one is not optional.
- **UFSD**, only if you want the UFS namespace — see below.
- A userid authorised to submit jobs, to update a PROCLIB in the started-task
  concatenation, and to update a PARMLIB.
- A way to upload a **binary** file to the host — see step 3.
- Roughly 15 cylinders of DASD: 10 for the two libraries the allocation job
  creates, the rest for the two TSO RECEIVE targets. One of those, the staging
  library, is scratched again by the last step of the install job.

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

### RAKF is required for logins

FTPD has no user database of its own. Every `USER`/`PASS` is verified by RAKF
(RACINIT, SVC 244), and every dataset access is checked against the RAKF
`DATASET` class under the logged-in user's identity. **On a system without RAKF
nobody can log in**, and no configuration option turns that off.

The setup — the `FTPD` user, the `USER` group and the `FTPAUTH` FACILITY
profile — is step 8, and the full reference is
[FTPD_RAKF_SETUP.md](https://github.com/mvslovers/ftpd/blob/main/doc/FTPD_RAKF_SETUP.md).

### Authorisation

FTPD is link-edited `AC(1)` and needs to be APF-authorised for two things: the
startup RACINIT that gives the started task its own identity, and the ACEE
switch that makes each session's dataset I/O run under the client's identity
rather than the server's.

It obtains authorisation itself at startup via `clib_apf_setup()`, which goes
through **SVC 244 — and that comes from RAKF**, which you need anyway. The
clean alternative is to add `FTPD.<vrm>.LINKLIB` to the APF list in
`SYS1.PARMLIB(IEAAPF00)`. On MVS 3.8j the APF list is only read at IPL, and the
library name carries the version — so this is one IPL per release, not one IPL
ever.

Unlike UFSD, FTPD **warns and keeps running** when authorisation fails:

```
FTPD003W APF SETUP FAILED RC=n
FTPD004W RACINIT FAILED: CANNOT ENTER SUPERVISOR STATE
```

The server starts and accepts connections, but the commands that need
authorisation fail one by one and say so. Treat those two messages as an
install that is not finished.

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

(A `quote site …` is accepted but its parameters are ignored — you do not need
it.)

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

Submit `ftpd-<version>-alloc.jcl` unchanged, unless you want a specific unit or
volume — the `UNIT=SYSDA` and the space on each DD are the only things worth
editing.

It creates `FTPD.<vrm>.LINKLIB` and `FTPD.<vrm>.AFTPDLOD` and nothing else. The
libraries the next step receives into are deliberately **not** allocated here:
TSO RECEIVE creates its own target and refuses to merge into an existing
dataset.

Expect `COND CODE 0000`.

> **Run this once.** There is no DELETE step in it, on purpose. After the
> install, `FTPD.<vrm>.AFTPDLOD` holds SMP's accepted copy of the module; a
> re-run that scratched it would leave the SMP inventory reporting an install
> that is no longer on the system, and nothing would say so. To start over,
> reject the SYSMOD first — see [Removing FTPD](#11-removing-ftpd).

---

## 5. Stop a running FTPD

Only relevant when you are upgrading. The APPLY writes into
`FTPD.<vrm>.LINKLIB`, and each release has its own — so a running *older* FTPD
does not block the install. It does, however, keep running the old module until
you restart it (step 9) against the procedure you copy in step 7.

```
/P FTPD
```

---

## 6. Install

Open `ftpd-<version>-inst.jcl` and replace the two placeholder dataset names
with what you uploaded in step 3:

```
  RECEIVE INDSN('CHANGE.ME.FTPDLOAD') -      <- IBMUSER.FTPD.LOAD.XMIT
  RECEIVE INDSN('CHANGE.ME.SAMPLIB') -       <- IBMUSER.FTPD.SAMP.XMIT
```

Those are the only lines you have to change. Submit it.

The job runs eight steps, each conditional on the one before, so it stops at
the first failure rather than building on it:

| Step | What it does |
|------|--------------|
| `DELOLD` | scratches the RECEIVE targets, so the job can be re-run |
| `RECV1` | load XMIT → `FTPD.<vrm>.FTPDLOAD` (a staging library) |
| `RECV2` | samplib XMIT → `FTPD.<vrm>.SAMPLIB` |
| `RECV` | receives the SYSMOD into the SMP inventory |
| `APPLYCHK` | dry run — `APPLY` only proceeds if this ends RC 0 |
| `APPLY` | copies the load module into `FTPD.<vrm>.LINKLIB` |
| `ACCEPT` | makes this level the base a later `RESTORE` returns to |
| `CLEANUP` | scratches the staging library, which is now spent |

The SYSMOD travels inline in the job — there is no third file to upload.

**What a good run looks like.** Every step `COND CODE 0000`, and in the SMP
output:

```
HMA3930    SYSMOD TFTP100 SUCCESSFULLY RECEIVED
HMA2380    COPY SUCCESSFUL - MOD=FTPD - LMOD=FTPD - LIBRARY=LINKLIB
           - RETURN CODE=00
HMA2050    APPLY PROCESSING COMPLETED - HIGHEST RETURN CODE IS 00
```

Then check `FTPD.<vrm>.LINKLIB` really holds `FTPD` (ISPF 3.4). Do look: SMP
reports the library by **ddname**, and a ddname says nothing about which dataset
was behind it.

SMP **copies** this module rather than re-binding it, which is why the `AC(1)`
authorisation code and the link attributes are exactly what the build produced.

---

## 7. Install the procedure and the configuration

SMP does not touch your PROCLIB or PARMLIB, and that is deliberate: **the
product owns the patterns, your system owns the copies.** If SMP owned the
running procedure, every change you made to it would be silently replaced by
the next update. So this step is yours, and it is the one place where you have
to read what you are copying.

Copy from `FTPD.<vrm>.SAMPLIB`:

| Member | Copy to | Adjust |
|--------|---------|--------|
| `FTPD` | a PROCLIB in the started-task concatenation | **yes — the JES DDs, see below**. `STEPLIB` already names this release's LINKLIB |
| `FTPDPRM0` | a PARMLIB | **yes — see below** |

`SYS2.PROCLIB` is the usual home for the procedure.

### Which PARMLIB

The shipped procedure defaults to `D='SYS2.PARMLIB'`. **On TK5 that dataset
does not exist** — put the member in `SYS1.PARMLIB` and either edit `D=` in
your copy of the procedure, or override it when starting:

```
/S FTPD,D='SYS1.PARMLIB'
```

On MVS/CE, `SYS2.PARMLIB` exists and the default is fine. `M=` selects the
member (default `FTPDPRM0`), so a second configuration can live alongside the
first.

### The JES DDs — these carry development-system values

`SITE FILETYPE=JES` (job submission and spool retrieval) needs **both** the JES2
checkpoint and the spool dataset, and the shipped procedure names them with the
unit and volume of the system it was built on:

```
//HASPCKPT DD  DISP=SHR,DSN=SYS1.HASPCKPT,UNIT=3350,VOL=SER=MVS000
//HASPACE1 DD  DISP=SHR,DSN=SYS1.HASPACE,UNIT=3350,VOL=SER=SPOOL1
```

Change `UNIT` and `VOL=SER` to the volumes your JES2 checkpoint and spool
datasets actually live on. Both DDs are required: with `HASPACE1` missing every
JES command answers `451` and the started task log shows
`IEC130I HASPACE1 DD STATEMENT MISSING`.

If you do not want the JES interface at all, delete both DDs — the rest of FTPD
is unaffected.

### Editing FTPDPRM0

The shipped member is a working default, but three settings are worth a look
before the first start:

```
SRVPORT=2121
PASVPORTS=22000-22200
MAXSESSIONS=10
IDLETIMEOUT=300
DEFUNIT=3390
DEFVOLUME=PUB001
```

- `SRVPORT` is 2121, not 21 — nothing else on the system has to move out of the
  way, and an unprivileged client reaches it with `ftp -P 2121`.
- `PASVPORTS` must be a range your firewall or NAT lets through, or passive
  mode stalls after `227`.
- `DEFUNIT`/`DEFVOLUME` are where FTPD allocates a dataset a client creates
  without saying where. `PUB001` is a development volume — name one that exists
  on your system.
- `SSLPROXY=YES` is only for a TLS-terminating proxy in front of FTPD. Run bare
  it promises clients a confidentiality it does not deliver; the member's own
  comments spell this out.

A `#` in the first non-blank column comments a line out — it has to be first,
because a `#` anywhere else is data.

---

## 8. Set up RAKF

Without this, FTPD starts and refuses every login. The short version:

1. Add the `FTPD` user to `SYS1.SECURE.CNTL(USERS)`, default group `USER`:

   ```
   FTPD     USER  DEFAULT-GROUP(USER)
   ```

2. Optionally define the `FTPAUTH` FACILITY profile, which controls who may use
   FTP at all. If it is **not** defined, RAKF answers "undefined resource" and
   every authenticated user is let in — acceptable for development, not for
   production:

   ```
   FTPAUTH  FACILITY  UACC(NONE)  ID(USER READ)
   ```

3. Reload: `/F RAKF,RELOAD`

**One security invariant:** `FTPD/USER` must stay least-privilege — no broad
`ID(FTPD) ACCESS(ALTER)` dataset profiles. Per-user access is authorised
against the *logged-in user's* identity, and the ABEND recovery path can
transiently put a concurrent session on the `FTPD/USER` ACEE. While that
identity is minimally privileged the switch can only lose authority; give it
broad authority and the same switch becomes fail-open.

The complete reference — dataset access levels per FTP command, per-user HLQ
patterns, RAKFCL equivalents and the reasoning behind the invariant — is
[FTPD_RAKF_SETUP.md](https://github.com/mvslovers/ftpd/blob/main/doc/FTPD_RAKF_SETUP.md).

---

## 9. Start and verify

```
/S FTPD                      default member (FTPDPRM0)
/S FTPD,M=FTPDPRM1           alternate config member
/S FTPD,D='SYS1.PARMLIB'     alternate PARMLIB -- TK5, see step 7
```

A healthy start ends in `FTPD001I … READY`:

```
FTPD000I FTPD <version> (A3F2C91) STARTING
FTPD005I LIBC370 V1.0.2 (22B4870)
FTPD004I STC IDENTITY SET TO FTPD/USER VIA RACINIT
FTPD054I LISTENING ON 0.0.0.0 PORT 2121
FTPD001I FTPD <version> READY
```

The two build stamps identify exactly what is running: `FTPD000I` gives the
version and the commit it was built from, `FTPD005I` the libc370 it was linked
against — quote both in a bug report. A build made from a modified working tree
marks its hash `-DIRTY` and adds `FTPD006W`; a released build never does.

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

## 10. What FTPD can reach

Three namespaces, selected per session:

- **MVS datasets** — the default. Sequential datasets and PDS members, with
  z/OS-compatible `SITE` keywords for `RECFM`, `LRECL`, `BLKSIZE`, `SPACE`,
  `UNIT` and `VOLUME`.
- **JES** — `quote site filetype=jes`; submit a job, list your jobs, retrieve
  spool output. Needs the two DDs from step 7.
- **UFS** — the UFSD filesystem, reached by `cd` to a UFS path. Needs the UFSD
  started task to be *running*, not just installed; without it those commands
  answer `550 UFS service not available`.

The command set and the `SITE` keywords are documented in
[ZOS_FTP_REFERENCE.md](https://github.com/mvslovers/ftpd/blob/main/doc/ZOS_FTP_REFERENCE.md);
the architecture is in
[FTPD_CONCEPT.md](https://github.com/mvslovers/ftpd/blob/main/doc/FTPD_CONCEPT.md).

---

## 11. Removing FTPD

Because the installation is SMP-managed, there is a defined way back — but it
is **not** the `RESTORE` followed by `REJECT` that SMP documentation leads you
to expect. The install job accepts the FMID in the same run as the APPLY, and
an accepted function SYSMOD refuses both: `RESTORE` because it was accepted,
`REJECT` because accepting removed the control statements it works from. The
route that does work is a `UCLIN` job.

The full procedure, with the job to submit and the messages to check, is in
[uninstall.md](https://github.com/mvslovers/ftpd/blob/main/doc/uninstall.md).

What SMP does **not** remove, because it never owned them: the copies you made
in step 7, and your RAKF definitions. Those are yours to delete.

---

## Troubleshooting

| Symptom | Likely cause |
|---------|--------------|
| Allocation job fails, dataset already exists | It was already run. Do not force it — see the warning in step 4 |
| `RECV1`/`RECV2` fails, target exists | Something else allocated it. RECEIVE refuses to merge; scratch it and re-run |
| `APPLYCHK` ends non-zero, `APPLY` skipped | Read the SMP output — the check exists to stop before anything is written. A missing DD is the usual cause |
| SMP reports success, but the module is not where you expected | A ddname says nothing about the dataset behind it. Check the JCL, then look at the library itself |
| `S806` (module not found) at `/S FTPD` | `STEPLIB` in the procedure does not name the LINKLIB the APPLY wrote to |
| `/S FTPD` rejected — procedure not found | Procedure not copied into a PROCLIB in the started-task concatenation |
| `FTPD003W APF SETUP FAILED` / `FTPD004W RACINIT FAILED` | No RAKF (so no SVC 244) and no APF entry — step 2 |
| `FTPD002E FTPD IS ALREADY ACTIVE ON PORT n` | An older instance is still up. `/P FTPD` first |
| `530 Login incorrect` for a valid user | RAKF user or password wrong — step 8 |
| `530 Not authorized for FTP access` | The `FTPAUTH` FACILITY profile denies this user — step 8 |
| `550 Access denied to <dsn>` | A RAKF `DATASET` profile denies it, checked under the client's identity |
| `451` on every JES command | `HASPCKPT`/`HASPACE1` missing or wrong in the procedure — step 7 |
| `550 UFS service not available` | The UFSD started task is not running (installed is not enough) |
| Passive transfer stalls after `227` | `PASVPORTS` not reachable, or `PASVADR` wrong behind NAT — step 7 |
| `S106` at start on a freshly installed library | The XMIT was uploaded in text mode. Re-upload in **binary** and re-run the install job |

For the RAKF message reference, see
[FTPD_RAKF_SETUP.md](https://github.com/mvslovers/ftpd/blob/main/doc/FTPD_RAKF_SETUP.md).
