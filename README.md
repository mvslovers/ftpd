# FTPD — Standalone FTP Server for MVS 3.8j

A full-featured FTP server for MVS 3.8j, built from the ground up with [crent370](https://github.com/mvslovers/crent370). Supports native MVS datasets, JES2 job management, and UFS files via [UFSD](https://github.com/mvslovers/ufsd) — with seamless switching between all three in a single session.

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
- STC runs as `FTPD/USER` via RACINIT (Principle of Least Privilege)
- See [doc/FTPD_RAKF_SETUP.md](doc/FTPD_RAKF_SETUP.md) for setup instructions

### Client Compatibility
- Verified identical behavior to z/OS 3.1 FTP server
- Tested with: FileZilla, ncftp, tnftp, curl, IBM PCOMM, Zowe CLI
- See [doc/COMPAT.md](doc/COMPAT.md) for details

## Quick Start

### Prerequisites
- MVS 3.8j with [crent370](https://github.com/mvslovers/crent370) runtime
- [RAKF](https://github.com/MVS-sysgen/RAKF) configured
- [mbt](https://github.com/mvslovers/mbt) build tool
- Optional: [UFSD](https://github.com/mvslovers/ufsd) for UFS file access

### Build
```bash
git clone https://github.com/mvslovers/ftpd.git
cd ftpd
# configure .env file with your MVS host settings
vi .env
make bootstrap build link
```

### Install
```
# Copy STC procedure and config to MVS
copy samplib/ftpd    to SYS2.PROCLIB
copy samplib/ftpdpm00 to SYS2.PARMLIB
```

### RAKF Setup
1. Add user `FTPD` in group `USER` to `SYS1.SECURE.CNTL(USERS)`
2. Add `FTPAUTH` profile to `SYS1.SECURE.CNTL(PROFILES)`
3. Recycle RAKF: `/S RAKF`
4. See [doc/FTPD_RAKF_SETUP.md](doc/FTPD_RAKF_SETUP.md) for details

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
| APPE | Append to dataset |
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
| PRIMARY=*n* | Primary space allocation |
| SECONDARY=*n* | Secondary space allocation |
| TRACKS / CYLINDERS | Space allocation unit |
| FILETYPE=JES / SEQ | Switch to JES or dataset mode |
| JESOWNER=*name* | JES job owner filter |
| JESJOBNAME=*name* | JES job name filter |
| JESSTATUS=*status* | JES job status filter (OUTPUT, ALL) |
| JESINTERFACELEVEL=*n* | JES interface level (1 or 2) |
| TRAILING | Toggle trailing blanks in text transfers |

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
| [crent370](https://github.com/mvslovers/crent370) | Yes | C runtime, RAKF, JES2, socket layer |
| [RAKF](https://github.com/MVS-sysgen/RAKF) | Yes | Authentication and authorization |
| [UFSD](https://github.com/mvslovers/ufsd) | No | UFS filesystem access (soft dependency) |
| [mbt](https://github.com/mvslovers/mbt) | Build | Build tool |

## License

See [LICENSE](LICENSE).
