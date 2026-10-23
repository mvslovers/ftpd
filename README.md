# FTPD — Standalone FTP Server for MVS 3.8j

A full-featured FTP server for MVS 3.8j, built from the ground up on [libc370](https://github.com/mvslovers/libc370). Supports native MVS datasets, JES2 job management, and UFS files via [UFSD](https://github.com/mvslovers/ufsd) — with seamless switching between all three in a single session.

## Features

### MVS Dataset Access
- Browse, upload, download, delete, rename datasets
- PDS member listing with ISPF statistics
- Binary roundtrip verified (MD5 match)
- Text transfers with full CP037 special character support (`[]{}|$@#`)
- Auto-allocation of new datasets via SITE parameters
- Catalog-based listing (no VTOC startup scan, instant startup)
- Wildcard filtering: `*.LOAD`, `**.DATA`, `R*` (member filter)

### JES2 Interface (`SITE FILETYPE=JES`)
- Job submission via internal reader
- Automatic USER/PASSWORD/NOTIFY injection into JOB card
- Job status query with z/OS-compatible listing format
- Spool retrieval (all DDs or specific spool file)
- Job purge

### UFS Filesystem (`CWD /path`)
- Full Unix-style file and directory operations via UFSD
- Hybrid navigation: switch between MVS and UFS in one session
- Soft dependency: works without UFSD (MVS + JES still available)

### Security
- RAKF authentication (userid/password)
- FACILITY class `FTPAUTH` for FTP access control
- Per-operation DATASET class authorization (READ/UPDATE/ALTER)
- ACEE switching: dataset I/O runs under logged-in user's identity
- STC keeps the identity RAKF assigns it; map the STC name to a low-privilege identity (Principle of Least Privilege)
- See [docs/FTPD_RAKF_SETUP.md](docs/FTPD_RAKF_SETUP.md) for setup instructions

### Client Compatibility
- Verified identical behavior to z/OS 3.1 FTP server
- Tested with: FileZilla, ncftp, tnftp, curl, IBM PCOMM, Zowe CLI
- See [internals/ZOS_FTP_REFERENCE.md](internals/ZOS_FTP_REFERENCE.md) for the reference behaviour

## Quick Start

### Prerequisites
- MVS 3.8j
- [RAKF](https://github.com/MVS-sysgen/RAKF) configured
- [mbt](https://github.com/mvslovers/mbt) 3 build tool, on your `PATH`
- Optional: [UFSD](https://github.com/mvslovers/ufsd) for UFS file access

**Build host: the cc370 toolchain with libc370 2.0.0 or later as its sysroot**
(`[toolchain] libc370` in `mbt.toml`; mbt refuses an older sysroot). The
runtime is linked statically, so nothing of libc370 has to be installed on MVS.

### Build
```bash
git clone https://github.com/mvslovers/ftpd.git
cd ftpd
mbt deps            # fetch ufsd's libufs, pinned in mbt.lock
mbt build           # the FTPD load module, under build/
mbt test            # the host tests
mbt package         # the SMP install package, under dist/
```

`mbt deploy` and `mbt test --mvs` talk to an MVS system through mvsMF. mbt
keeps those systems in `~/.mbt/targets.toml`, once per machine; set one up
with `mbt target import .env --name <name>` from an mbt 2 `.env`, or see the
[mbt migration guide](https://github.com/mvslovers/mbt/blob/main/docs/MIGRATION.md#5-mvs-systems-from-env-to-targets).

### Install
```
# Copy STC procedure and config to MVS
copy samplib/ftpd    to SYS2.PROCLIB
copy samplib/ftpdpm00 to SYS2.PARMLIB
```

### RAKF Setup
1. Map the started task name `FTPD` to a low-privilege identity (Operations `N`) where your RAKF supports it; FTPD no longer needs a user of its own. **Without such a mapping, FTPD runs as `STC`/`STCGROUP` with Operations authority.** That is the case on RAKF 1.2.x (TK4-, TK5 before Update 6); RAKF 2.0.0 maps the name from a user `FTPD` in `USERS`, whose group must be allowed `FACILITY SVC244`. Running as `STC`/`STCGROUP` breaks the security invariant: the recovery path can then fail open. Check `FTPD004I STC RUNS AS <user>/<group>` at startup, and read [Why the started task's identity matters](docs/FTPD_RAKF_SETUP.md#5-why-the-started-tasks-identity-matters)
2. Create the `FTPAUTH` profile and let your group read it, with the RAKF commands: `RDEFINE FTPAUTH CLASS(FACILITY) UACC(NONE)` and `PERMIT FTPAUTH CLASS(FACILITY) ID(group) ACCESS(READ)` (without the profile RAKF grants ALTER, so everybody gets in). **Do not edit `PROFILES` by hand; RAKF's maintainer recommends the commands.** On MVS/CE the profile is already there and `RDEFINE` answers `Profile already defined` (details in the RAKF guide, section 3)
3. Reload the tables: `/S RAKF` (it reloads the users and the profiles; `/S RAKFPROF` reloads only the profiles)
4. See [docs/FTPD_RAKF_SETUP.md](docs/FTPD_RAKF_SETUP.md) for details

### Start
```
/S FTPD
```

### Connect
```bash
ftp mvshost 2121
```

## Supported FTP Commands

| Command | Description |
|---------|-------------|
| USER/PASS | Authentication via RAKF |
| SYST | System type (215 MVS) |
| FEAT | Feature negotiation |
| CWD/CDUP/PWD | Directory navigation (MVS prefix + UFS paths) |
| LIST/NLST | Directory listing (z/OS MVS format / Unix format) |
| RETR | Download dataset, PDS member, or UFS file |
| STOR | Upload (auto-allocates new datasets) |
| APPE | Append to a sequential dataset (creates what is missing); an existing PDS member is refused with 550, UFS answers 502 |
| DELE | Delete dataset, PDS member, or UFS file |
| MKD/RMD | Create/remove PDS or UFS directory |
| RNFR/RNTO | Rename dataset or UFS file |
| SIZE | File/dataset size query |
| MDTM | Modification timestamp (UFS) |
| TYPE A/I/E | ASCII, binary, EBCDIC transfer modes |
| PORT/PASV | Active and passive data connections |
| EPSV | Extended passive mode (RFC 2428) |
| SITE | Allocation parameters, JES mode, toggles |
| STAT/NOOP/HELP | Session info, keepalive, help |
| QUIT | Disconnect |

## SITE Subcommands

| Subcommand | Description |
|------------|-------------|
| RECFM=*fmt* | Record format (FB, VB, F, V, U) |
| LRECL=*n* | Logical record length |
| BLKSIZE=*n* | Block size |
| DCBDSN=*dsn* | Take RECFM, LRECL and BLKSIZE from the cataloged data set *dsn* (fully qualified, quotes optional) for the data sets created next; a bare `DCBDSN` returns the three to their defaults |
| PRIMARY=*n* | Primary space allocation (1-16777215) |
| SECONDARY=*n* | Secondary space allocation (0-16777215; 0 = none) |
| DIRECTORY=*n* | PDS directory blocks (1-16777215), default `DEFDIRECTORY` |
| RETPD=*days* | Retention period of new data sets (0-9999; 0 = none), at most `MAXRETPD` days |
| TRACKS / CYLINDERS / BLOCKS | Space allocation unit |
| FILETYPE=JES / SEQ | Switch to JES or dataset mode |
| JESOWNER=*name* | JES job owner filter |
| JESJOBNAME=*name* | JES job name filter |
| JESSTATUS=*status* | JES job status filter: ALL (default), INPUT, ACTIVE or OUTPUT; applies to listing, retrieving and deleting |
| TRAILING | Toggle trailing blanks in text transfers |
| TRUNCATE | Toggle cutting of text lines longer than the record: off, the upload fails with `451`; on, the line is cut and the `250` says so. A failed upload scratches a data set it created, but leaves an existing one incomplete |

`SITE RDW` and `SITE SBSENDEOL=` with anything but `CRLF` are answered `501`
(`SITE SBSENDEOL` alone returns to the default, `CRLF`):
FTPD keeps no record descriptor words and only handles CRLF line ends.

A parameter given without a value returns to the configured default — so
`SITE PRIMARY` undoes an earlier `SITE PRIMARY=200` for the rest of the
session (`SITE RETPD` returns to no retention). `BLOCKS` counts the space in
units of the current `BLKSIZE`.

`SITE DCBDSN=dsn` copies the three values at once, so the last of `DCBDSN`,
`RECFM`, `LRECL` and `BLKSIZE` given wins for each value. A model that is not
cataloged, has no readable DSCB or no RECFM, or that the user may not read, is
answered `501` and nothing changes. RECFM `S`, `T` and `M` are not carried
over; the `200` names it when the model has one.

`SITE RETPD=n` with `n` above `MAXRETPD` in `FTPDPRM` (default 365 days) is
answered `501` naming the limit, and the session keeps its retention period.

A data set whose retention period has not expired is not opened for output:
`STOR` and `APPE` answer `550` and name the expiration date, instead of waiting
for an operator reply (IEC507D). That includes a data set carrying the
expiration date 99000, which is no calendar date but is retained all the same.
A PDS created with `SITE RETPD` is retained as
a whole, so it takes no member until the period is over. `DELE` and `RMD` still
remove a retained data set.

Keywords can be abbreviated the way z/OS allows, so scripts written for z/OS
work unchanged: `PRI=`, `SEC=`, `TR`, `CY`, `REC=`, `LR=`, `BLK=`, `DIR=`,
`D=`, `VOL=`, `U=`, `RET=` and so on. A form that fits more than one keyword
or is too short (`PR=`, `P=`, `T`) is answered with `Unrecognized parameter`
and changes nothing.

The verb, the keywords and their values are case-insensitive: `SITE`, `site`
and `Site` are one command, as are `LRECL=80` and `lrecl=80`, `RECFM=FB` and
`recfm=fb`, `FILETYPE=JES` and `filetype=jes`. FTPD upper-cases all of them
(JES job names, owners and statuses included) before it acts on them.

The BSD `ftp` client and tnftp (the `ftp` of many Linux, BSD and macOS systems)
implement `site` as a command of their own, which sends `SITE <arguments>` to
the server. They do not know `SITE` or `Site`: the client answers
`?Invalid command.` without contacting FTPD. Type `site` in lower case, or use
`quote`, which sends the line to the server unchanged, in any spelling:

```
ftp> site lrecl=80
200 SITE command was accepted
ftp> quote SITE RECFM=FB
200 SITE command was accepted
ftp> quote Site FILETYPE=JES
200 SITE command was accepted
```

Allocation parameters are sticky for the session and apply to a dataset FTPD
has to create; their starting values come from `DEFPRIMARY`, `DEFSECONDARY`
and `DEFSPACETYPE` in `FTPDPRM`.

## Architecture

```
Client ──► Control Connection (port 2121)
              │
              ├── ftpd#cmd.c  (command dispatcher)
              ├── ftpd#aut.c  (RAKF authentication)
              ├── ftpd#mvs.c  (MVS dataset operations)
              ├── ftpd#jes.c  (JES2 interface)
              ├── ftpd#ufs.c  (UFS operations via UFSD)
              ├── ftpd#lst.c  (LIST formatting)
              └── ftpd#sit.c  (SITE command handling)
              │
              ├── ftpd#dat.c  (data connections PORT/PASV/EPSV)
              └── ftpd#xlt.c  (EBCDIC/ASCII translation)
```

## Dependencies

| Dependency | Required | Purpose |
|------------|----------|---------|
| [libc370](https://github.com/mvslovers/libc370) | Build | C runtime, RAKF, JES2, socket layer (statically linked, from the cc370 sysroot) |
| [RAKF](https://github.com/MVS-sysgen/RAKF) | Yes | Authentication and authorization |
| [UFSD](https://github.com/mvslovers/ufsd) | No | UFS filesystem access (soft dependency) |
| [mbt](https://github.com/mvslovers/mbt) | Build | Build tool |

## License

See [LICENSE](LICENSE).
