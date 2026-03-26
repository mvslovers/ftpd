# FTPD for MVS 3.8j — Concept & Architecture Document

**Project:** FTPD — A Modern FTP Server for MVS 3.8j  
**Version:** 0.2 (Draft)  
**Date:** 2026-03-12  
**Author:** Concept developed collaboratively  
**Status:** Concept Phase

---

## 1. Motivation & Goals

### 1.1 Background

There are currently two FTP servers available for MVS 3.8j on Hercules:

**HTTPD-integrated FTP (mvslovers/httpd)**
- Part of Michael Dean Rayborn's multi-threaded HTTPD server
- Runs on a separate port (default 8021) alongside the HTTP server
- Built with UFS (Unix-like Filesystem) support in mind — uses the old UFS370 library
- Multi-threaded architecture with worker pool
- EBCDIC-internal with ASCII conversion at network I/O boundaries
- Depends on CRENT370, UFS370, and other Rayborn libraries

**MVS-sysgen FTPD (Jason Winter / Juergen Winkelmann)**
- Standalone FTP daemon (FTPDXCTL)
- Built for native MVS datasets — reads VTOC from configured DASD volumes
- RAKF authentication (FACILITY class FTPAUTH)
- Configuration via SYS1.PARMLIB(FTPDPM00)
- Internal reader DD (AAINTRDR) for JES job submission
- Passive mode support, AUTHUSER for remote shutdown
- Library Optimisation Extensions (FAST mode)
- Originally built with the JCC compiler

Both servers work, but each has significant limitations. The HTTPD FTP server is tightly coupled to the HTTP server and focuses on UFS, while the MVS-sysgen FTPD handles native MVS datasets but lacks UFS support and modern z/OS FTP features.

### 1.2 Project Goal

Build a **new, standalone FTP server** for MVS 3.8j that:

1. Supports **both native MVS datasets AND UFS** via UFSD (the new Cross-Address-Space Filesystem Server)
2. Implements **z/OS-compatible FTP features** (SITE FILETYPE=JES, job submission, spool retrieval)
3. Is a **standalone daemon** — not tied to HTTPD
4. Uses **mbt** (MVS Build Tool) with `project.toml` for the build pipeline
5. Uses **crent370** as C runtime (including its `thdmgr`, `jes`, `racf`, `os` modules)
6. Has a **clean, modular architecture** designed for maintainability
7. Provides **RAKF/credentials-based authentication** via crent370's `racf` module

### 1.3 Non-Goals (v1.0)

- TLS/FTPS encryption (can be handled at the Hercules level)
- FTP client implementation
- DB2 SQL interface (z/OS SITE FILETYPE=SQL)
- Anonymous FTP (may be added later)
- IPv6 support
- Full z/OS codepage zoo (SJISKANJI, BIG5, UCS-2, etc. — we support ASCII, EBCDIC, Binary)

### 1.4 z/OS FTP Reference

The z/OS FTP server is our primary inspiration for feature selection. Key reference documents:

- IBM z/OS 2.5.0 FTP subcommands reference
- IBM z/OS basic skills: FTP server overview
- IBM z/OS Communications Server: IP User's Guide (SITE command, JES interface)
- Colin Paice's blog: Setting up FTP server on z/OS
- **`ZOS_FTP_REFERENCE.md`** — Protocol captures from z/OS 3.1 FTP server (IBM FTP CS 3.2). Contains exact response strings, error codes, LIST formats, transfer behavior, and SITE command semantics. This is the authoritative reference for our z/OS compatibility implementation.

We cherry-pick the features that make sense for MVS 3.8j. Features requiring USS, RACF (not RAKF), SMS, PDSE, or other z/OS-only infrastructure are excluded.

---

## 2. Feature Specification

### 2.1 Core FTP Protocol (RFC 959)

Standard FTP commands that MUST be supported:

| Command | Description |
|---------|-------------|
| `USER` / `PASS` | Authentication |
| `SYST` | System type (mode-aware: `215 MVS` in dataset mode, `215 UNIX` in UFS mode) |
| `TYPE` | Transfer type (A=ASCII, I=Image/Binary, E=EBCDIC, L 8=synonym for I) |
| `MODE` | Transfer mode (S=Stream) |
| `STRU` | File structure (F=File, R=Record) |
| `PORT` | Active mode data connection |
| `PASV` | Passive mode data connection |
| `LIST` / `NLST` | Directory listing / name list |
| `RETR` | Retrieve/download file |
| `STOR` | Store/upload file |
| `APPE` | Append to file (creates dataset if nonexistent) |
| `DELE` | Delete file |
| `RNFR` / `RNTO` | Rename file |
| `MKD` / `RMD` | Make/remove directory (UFS) or allocate/scratch PDS (MVS) |
| `CWD` / `CDUP` | Change working directory |
| `PWD` | Print working directory |
| `STAT` | Server status |
| `NOOP` | No operation (keepalive) |
| `QUIT` | Disconnect |
| `ABOR` | Abort transfer |
| `REST` | Restart transfer (RFC 3659) |
| `FEAT` | Feature negotiation (RFC 2389) |
| `SIZE` | File size (RFC 3659) |
| `MDTM` | File modification time (RFC 3659) |
| `HELP` | Command help |

### 2.2 z/OS-Compatible SITE Commands

The SITE command is the key differentiator. We implement the subset that makes sense on MVS 3.8j:

**Bare SITE (no arguments):**
- Returns `202 SITE not necessary; you may proceed` (matches z/OS behavior)
- Current settings are displayed via `STAT`, not via bare SITE

**Filesystem Mode Switching:**
- `SITE FILETYPE=SEQ` — Normal dataset/file mode (default)
- `SITE FILETYPE=JES` — JES interface mode (job submission/retrieval)

**Dataset Allocation Parameters (for STOR on new datasets):**
- `SITE RECFM=xx` — Record format (FB, VB, F, V, U, FBA, VBA)
- `SITE LRECL=nnn` — Logical record length
- `SITE BLKSIZE=nnn` — Block size (auto-adjusted to valid LRECL multiple for FB)
- `SITE PRIMARY=nnn` — Primary space allocation
- `SITE SECONDARY=nnn` — Secondary space allocation
- `SITE TRACKS` / `SITE CYLINDERS` — Space allocation unit
- `SITE VOLUME=xxxxxx` — Target volume serial
- `SITE UNIT=xxxx` — Device type (3350, 3380, 3390)
- `SITE DIRECTORY=nn` — PDS directory blocks

All allocation parameters are **sticky** — they persist for the entire session until explicitly changed.

**BLKSIZE auto-adjustment:** If BLKSIZE is not a valid multiple of LRECL for FB datasets, the server rounds down to the nearest valid multiple and warns:
```
200-BLOCKSIZE must be a multiple of LRECL for RECFM FB
200-BLOCKSIZE being set to 6120
200 SITE command was accepted
```

**JES Interface Parameters:**
- `SITE JESINTERFACELEVEL=n` — JES interface level (1 or 2)
- `SITE JESJOBNAME=name` — Filter jobs by name
- `SITE JESOWNER=owner` — Filter jobs by owner (`*` for all own jobs)
- `SITE JESSTATUS=status` — Filter by status (INPUT, ACTIVE, OUTPUT, ALL)

**Transfer Control (toggle semantics — send once to enable, again to disable):**
- `SITE TRAILING` — Toggle trailing blank inclusion (default: blanks NOT removed, matching z/OS)
- `SITE TRUNCATE` — Toggle overlong record truncation (default: OFF)
- `SITE RDW` — Toggle Record Descriptor Words for VB datasets (default: OFF)
- `SITE SBSENDEOL=CRLF|LF` — End-of-line for submitted jobs

**Unknown/unrecognized SITE parameters:**
Per z/OS behavior, unknown parameters return `200` with a warning (not `502`):
```
200-Unrecognized parameter 'FOOBAR=123' on SITE command.
200 SITE command was accepted
```
This is lenient behavior for forward compatibility — clients that send z/OS-specific parameters (DATACLAS, STORCLAS, etc.) will not break.

### 2.3 MVS Dataset Support

**Dataset Name Handling (z/OS compatible):**
- Unqualified names are prefixed with the user's HLQ (userid): `MYDATA` → `'USERID.MYDATA'`
- Fully qualified names enclosed in single quotes: `'SYS1.PARMLIB(IEASYS00)'`
- PDS member access: `DATASET.NAME(MEMBER)`
- Wildcard support in LIST: `USER1.*.COBOL`, `USER1.SOURCE(MEM*)`

**CWD behavior (z/OS compatible):**
- `CWD` sets the dataset name prefix only — **no I/O, no existence check**. Always returns `250`.
- Exception: when the resolved name is an actual PDS, response text changes to indicate "partitioned data set"
- `CWD 'NONEXISTENT.'` → `250 "NONEXISTENT." is the working directory name prefix.` (succeeds)
- `CWD ..` / `CDUP` → removes last qualifier. At empty prefix, returns `250` with empty prefix (no error)
- The trailing dot is significant: prefix `IBMUSER.` means `RETR FOO` resolves to `IBMUSER.FOO`

**Supported Dataset Organizations:**
- PS (Physical Sequential)
- PO (Partitioned — PDS)
- DA (Direct Access) — read-only listing

**Dataset Operations:**
- `LIST` — Datasets matching current prefix with attributes (RECFM, LRECL, BLKSIZE, DSORG, volume). In DATASETMODE, `LIST 'PDS.NAME'` shows catalog entry, not members. Members listed via `LIST 'PDS(MEM*)'` or after CWD into the PDS.
- `LIST` on PDS — Members (with ISPF stats if available; blanks for members without stats)
- `RETR` — Read sequential dataset or PDS member
- `STOR` — Write sequential dataset or PDS member (replaces content if existing)
- `APPE` — Append to dataset; **creates the dataset** if nonexistent (uses current SITE allocation params)
- `DELE` — Delete dataset (including entire PDS with members) or PDS member
- `MKD` — Allocate a new PDS (using current SITE allocation params, DSORG=PO)
- `RMD` — Scratch a PDS (same as DELE for datasets)
- `RNFR`/`RNTO` — Rename dataset or PDS member. RNFR state is cleared on any command other than RNTO (stricter than z/OS which has a stale-RNFR bug). MVS mode rejects rename to existing target with `550`.

**z/OS-compatible LIST format for MVS datasets:**
```
Volume Unit    Referred Ext Used Recfm Lrecl BlkSz Dsorg Dsname
PUB001 3390   2026/03/12  1   15  FB      80 27920  PO  SYS1.PARMLIB
MVSRES 3350   2026/03/12  1    3  FB      80  3120  PS  SYS1.LOGREC
```

**PDS Member LIST format:**
```
 Name     VV.MM  Created   Changed     Size  Init   Mod   Id
 IEASYS00 01.05 2024/01/15 2025/06/20  45    45     3     IBMUSER
```

### 2.4 UFS (Unix-like Filesystem) Support via UFSD

UFS access uses the **UFSD client library** (mvslovers/ufsd). UFSD is a Cross-Address-Space Filesystem Server — our FTP server communicates with the UFSD subsystem running in its own address space via cross-memory services.

**Prerequisite:** UFSD must be running as a started task (`/S UFSD`).

When the working directory starts with `/`, the server switches to UFS mode:

- Standard Unix-like path navigation (`/`, `..`, relative paths)
- `LIST` — Unix-style long listing (`-rwxr-xr-x 1 user group size date name`)
- `MKD` / `RMD` — Create/remove directories
- `RETR` / `STOR` — Read/write files
- `DELE` — Delete files
- `RNFR` / `RNTO` — Rename files
- `SIZE` / `MDTM` — File metadata

**Mode Switching:**
- `CWD /` or `CWD /path` — Switch to UFS mode. `SYST` returns `215 UNIX` in this mode.
- `CWD 'HLQ.DATASET'` or `CWD dataset` — Switch back to MVS mode. `SYST` returns `215 MVS`.
- `PWD` reflects current context: `257 "'USER1.'" is working directory.` (MVS) or `257 "/home/user1" is the HFS working directory.` (UFS)
- UFS rename (`RNTO`) to existing target **overwrites** (Unix semantics), unlike MVS mode which rejects with `550`

### 2.5 JES Interface (SITE FILETYPE=JES)

When `SITE FILETYPE=JES` is active, FTP commands change meaning:

**Job Submission:**
- `STOR` / `PUT` — Submit JCL to JES internal reader
- Server responds with job number: `250-It is known to JES as JOBnnnnn`

**Job Listing:**
- `LIST` / `DIR` — List jobs in JES queues
- Filterable by JESOWNER, JESJOBNAME, JESSTATUS
- Format: `JOBNAME  JOBID   OWNER   STATUS  CLASS  RC`

**Job Output Retrieval:**
- `RETR JOBnnnnn` — Retrieve all spool output, DDs separated by ` !! END OF JES SPOOL FILE !!` (space-prefixed, exact z/OS convention)
- `RETR JOBnnnnn.n` — Retrieve specific spool file (DD) number n (numbering starts at **1**, not 0)

**Job Management:**
- `DELE JOBnnnnn` — Purge job from JES queues. Response: `250 Cancel successful` (matches z/OS)

**JES Interface Levels:**
- Level 1: Job name must match userid (or userid + 1 char). Simple, secure.
- Level 2: Any job name. `SITE JESOWNER=*` required to list all own jobs. More flexible.

Implementation uses crent370's `jes/` module for JES2 interaction (internal reader opened programmatically).

### 2.6 Transfer Mode Details

**ASCII Mode (TYPE A):**
- EBCDIC ↔ ASCII translation at transfer boundary (IBM-1047 ↔ ISO-8859-1)
- Record delimiters: CRLF (ASCII 0x0D 0x0A) in stream, dataset records on disk
- With STRU F (default): trailing blanks trimmed from FB records before CRLF
- With STRU R: trailing blanks preserved, RFC 959 record markers used

**EBCDIC Mode (TYPE E):**
- No translation — raw EBCDIC transfer
- Record separator: **EBCDIC NL (X'15')** after each record (not CRLF, not X'25')
- Useful for mainframe-to-mainframe transfers

**Binary/Image Mode (TYPE I):**
- No translation, no record processing, no record separators
- Byte-for-byte transfer: FB 80 dataset with 10 records = exactly 800 bytes
- Required for load modules, object decks, XMI files
- `TYPE L 8` accepted as synonym for TYPE I

**Record Structure (STRU R):**
- Preserves record boundaries in transfer using RFC 959 markers:
  - `X'FF01'` = EOR (End of Record) after each record
  - `X'FF02'` = EOF (End of File) at end of transfer
- Trailing blanks are **preserved** (unlike TYPE A + STRU F)
- Data content is translated per current TYPE (A → ASCII, E → EBCDIC, I → raw)
- Essential for VB (variable-length blocked) datasets

**SITE RDW (Record Descriptor Word):**
- Only meaningful for VB datasets. No effect on FB.
- Prepends 4-byte LLBB (LL = record length including RDW, BB = 0) per record
- Used with TYPE I for binary transfer with record length preservation

**Pre-authentication commands:**
Per z/OS behavior, the following commands are allowed before USER/PASS: SYST, FEAT, HELP, NOOP, STAT, SITE, QUIT. All data transfer and navigation commands require authentication (`530 Not logged in.`).

---

## 3. Architecture

### 3.1 High-Level Architecture

```
                    ┌──────────────────────────────────────┐
                    │            MVS 3.8j                   │
                    │                                       │
  FTP Client ──────┤►  ┌─────────────┐                     │
  (port 21/2121)   │   │  FTPD Main  │                     │
                   │   │  (Listener)  │                     │
                   │   └──────┬───────┘                     │
                   │          │ thdmgr thread                │
                   │   ┌──────┴───────┐                     │
                   │   │  Session     │                     │
                   │   │  Handler     │                     │
                   │   └──┬───┬───┬──┘                     │
                   │      │   │   │                         │
                   │   ┌──┴┐ ┌┴──┐┌┴──┐                    │
                   │   │MVS│ │UFS│ │JES│  ◄── Filesystem    │
                   │   │FS │ │FS │ │IF │      Backends      │
                   │   └─┬─┘ └─┬─┘ └─┬─┘                   │
                   │     │     │     │                      │
                   │     │  ┌──┴──┐  │                      │
                   │     │  │UFSD │  │   ◄── Cross-address  │
                   │     │  │(STC)│  │       space server   │
                   │     │  └─────┘  │                      │
                   │   ┌─┴───────────┴──┐                   │
                   │   │   crent370      │  ◄── C Runtime   │
                   │   │ (os, jes, racf, │      (sockets,   │
                   │   │  thdmgr, ipc)   │       threads,   │
                   │   └─────────────────┘       I/O)       │
                   └───────────────────────────────────────┘
```

### 3.2 Component Model

```
ftpd/
├── project.toml            # mbt project definition
├── Makefile                 # 2-line Makefile (MBT_ROOT + include)
├── .env                     # Local MVS connection (gitignored)
├── mbt/                     # mbt submodule
├── src/
│   ├── ftpd.c              # Main: listener loop, console commands, shutdown
│   ├── ftpdses.c           # Session handler (per-connection state machine)
│   ├── ftpdcmd.c           # FTP command parser & dispatcher
│   ├── ftpdmvs.c           # MVS dataset operations (VTOC, OBTAIN, OPEN/CLOSE)
│   ├── ftpdufs.c           # UFS operations (via UFSD client library)
│   ├── ftpdjes.c           # JES interface (submit, list, retrieve spool)
│   ├── ftpddata.c          # Data connection management (PORT/PASV)
│   ├── ftpdxlat.c          # EBCDIC ↔ ASCII translation tables
│   ├── ftpdauth.c          # Authentication (RAKF via crent370 racf module)
│   ├── ftpdsite.c          # SITE command processing
│   ├── ftpdlist.c          # LIST/NLST formatting (MVS + UFS + JES formats)
│   ├── ftpdlog.c           # Logging (WTO messages, STDOUT)
│   └── ftpdcfg.c           # Configuration file parsing
├── include/
│   ├── ftpd.h              # Main header, shared structures & constants
│   ├── ftpdses.h           # Session state definitions
│   ├── ftpdmvs.h           # MVS dataset structures
│   ├── ftpdufs.h           # UFS interface prototypes
│   ├── ftpdjes.h           # JES interface definitions
│   └── ftpdxlat.h          # Translation table declarations
├── asm/                     # Generated .s files (from c2asm370)
├── contrib/                 # Extracted dependency headers (gitignored)
└── jcl/
    ├── FTPD.jcl            # STC procedure for FTPD
    └── INSTALL.jcl          # Installation JCL
```

### 3.3 project.toml

```toml
[project]
name    = "ftpd"
version = "0.1.0"
type    = "application"

[mvs.build.datasets.source]
suffix  = "SOURCE"
dsorg   = "PO"
recfm   = "FB"
lrecl   = 80
blksize = 3120
space   = ["TRK", 10, 5, 20]

[mvs.build.datasets.punch]
suffix  = "OBJECT"
dsorg   = "PO"
recfm   = "FB"
lrecl   = 80
blksize = 3120
space   = ["TRK", 10, 5, 20]

[mvs.build.datasets.ncalib]
suffix  = "NCALIB"
dsorg   = "PO"
recfm   = "U"
lrecl   = 0
blksize = 32760
space   = ["TRK", 10, 5, 10]

[mvs.build.datasets.syslmod]
suffix  = "LOAD"
dsorg   = "PO"
recfm   = "U"
lrecl   = 0
blksize = 32760
space   = ["TRK", 10, 5, 5]

[dependencies]
"mvslovers/crent370" = ">=1.0.0"
"mvslovers/ufsd"     = ">=0.1.0"

[link.module]
name    = "FTPD"
options = ["LIST", "XREF", "LET"]

[artifacts]
mvs = true
```

### 3.4 Session State Machine

Each FTP connection is managed by a dedicated thread (via crent370's `thdmgr`):

```
  CONNECT
     │
     ▼
  SESS_GREETING ──► Send 220 banner
     │
     ▼
  SESS_AUTH_USER ──► Waiting for USER command
     │
     ▼
  SESS_AUTH_PASS ──► Waiting for PASS command
     │                   │
     │              (auth fail) ──► SESS_AUTH_USER (max 3 attempts)
     ▼
  SESS_READY ◄──────────────────────────────────┐
     │                                           │
     ▼                                           │
  SESS_COMMAND ──► Parse & dispatch command ──────┘
     │
     │──► SITE FILETYPE=JES ──► switch to JES mode
     │──► CWD /path          ──► switch to UFS mode
     │──► CWD 'dsn'          ──► switch to MVS mode
     │──► QUIT               ──► SESS_CLOSING
     │
  SESS_TRANSFER ──► Active data transfer
     │
     ▼
  SESS_READY (transfer complete)
```

### 3.5 Key Data Structures

```c
/* Session context — one per connected client */
typedef struct ftpd_session {
    int                 ctrl_sock;       /* Control connection socket    */
    int                 data_sock;       /* Data connection socket       */
    int                 pasv_sock;       /* Passive listen socket        */

    int                 state;           /* Session state                */
    int                 filetype;        /* FT_SEQ or FT_JES            */
    int                 fsmode;          /* FS_MVS or FS_UFS            */
    char                type;            /* 'A','E','I' transfer type    */
    char                stru;            /* 'F','R' structure            */

    /* Authentication */
    char                user[9];         /* Userid (8 chars + null)      */
    int                 authenticated;   /* Login complete flag          */

    /* MVS context */
    char                hlq[45];         /* High-level qualifier/prefix  */
    char                mvs_cwd[45];     /* Current MVS "directory"      */

    /* UFS context (via UFSD client) */
    char                ufs_cwd[256];    /* Current UFS path             */

    /* JES context */
    int                 jes_level;       /* JES interface level (1/2)    */
    char                jes_owner[9];    /* JESOWNER filter              */
    char                jes_jobname[9];  /* JESJOBNAME filter            */
    char                jes_status[8];   /* JESSTATUS filter             */

    /* SITE dataset allocation defaults */
    struct {
        char            recfm[4];        /* RECFM for new datasets       */
        int             lrecl;           /* LRECL                         */
        int             blksize;         /* BLKSIZE                       */
        int             primary;         /* Primary allocation            */
        int             secondary;       /* Secondary allocation          */
        char            spacetype[4];    /* TRK or CYL                   */
        char            volume[7];       /* Target volume                 */
        char            unit[5];         /* Device type                   */
        int             dirblks;         /* PDS directory blocks          */
    } alloc;

    /* Transfer state */
    long                rest_offset;     /* REST restart offset           */
    char                rnfr_path[256];  /* RNFR pending rename source    */

    /* Statistics */
    long                bytes_sent;
    long                bytes_recv;
    int                 xfer_count;
} ftpd_session_t;

/* Server configuration — parsed from PARMLIB at startup */
typedef struct ftpd_config {
    int                 port;            /* Listen port (default 21)     */
    char                bind_ip[16];     /* Bind IP ("ANY" = 0.0.0.0)   */
    char                pasv_addr[16];   /* PASV address for responses   */
    int                 pasv_lo;         /* PASV port range low          */
    int                 pasv_hi;         /* PASV port range high         */
    int                 max_sessions;    /* Maximum concurrent sessions  */
    int                 idle_timeout;    /* Idle timeout in seconds      */
    int                 jes_level;       /* Default JES interface level  */
    char                banner[80];      /* Custom 220 banner text       */
    char                authuser[9];     /* User allowed to TERM server  */
    int                 insecure;        /* Allow non-localhost connects */

    /* Default allocation parameters for new datasets */
    struct {
        char            recfm[4];
        int             lrecl;
        int             blksize;
        char            unit[5];
        char            volume[7];
    } defaults;

    /* DASD configuration for VTOC scanning */
    int                 num_dasd;
    struct {
        char            volser[7];       /* Volume serial                */
        char            unit[5];         /* Device type                  */
    } dasd[32];
} ftpd_config_t;
```

### 3.6 Threading Model

The server uses crent370's **thdmgr** (Thread Manager) for concurrency:

1. **Main thread:** Listener on control port, accepts connections, manages shutdown, processes MVS console commands
2. **Session threads:** One thread per client connection, managed by thdmgr. Each thread owns its session state and runs the command-response loop independently.

This is the same proven model used by HTTPD. The thdmgr handles thread creation, pooling, and cleanup. Socket I/O uses crent370's socket API.

### 3.7 Data Connection Handling

FTP uses a separate data connection for each transfer:

**Active Mode (PORT):**
1. Client sends `PORT h1,h2,h3,h4,p1,p2`
2. Server connects FROM port 20 TO client's address:port
3. Transfer occurs, data connection closed

**Passive Mode (PASV):**
1. Client sends `PASV`
2. Server opens listening socket on port from configured range
3. Server responds: `227 Entering Passive Mode (h1,h2,h3,h4,p1,p2)`
4. Client connects TO server's address:port
5. Transfer occurs, data connection closed

Passive mode is preferred by modern clients (firewalls, NAT). The configurable PASV address and port range are essential for Hercules environments where the guest IP differs from the host IP.

### 3.8 EBCDIC ↔ ASCII Translation

All internal processing is in **EBCDIC** (native MVS). Translation at the network boundary:

```
  Network (ASCII) ←→ [ftpdxlat] ←→ Internal (EBCDIC) ←→ MVS I/O / UFSD
```

- `TYPE A`: Full translation (IBM-1047 ↔ ISO-8859-1)
- `TYPE E`: No translation (EBCDIC on wire)
- `TYPE I`: No translation (binary)

---

## 4. MVS Integration Details

### 4.1 Dataset Access

**Reading datasets:**
- OBTAIN (SVC 27) to get DSCB from VTOC
- Dynamic allocation (SVC 99) to allocate the dataset
- OPEN/READ/CLOSE macros for sequential reading
- For PDS: Read directory block, then individual members via BPAM/FIND

**Writing datasets:**
- Existing datasets: Dynamic allocation + OPEN/WRITE/CLOSE
- New datasets: Use SITE allocation parameters with SVC 99
- PDS member creation: STOW macro after writing

**Dataset catalog operations:**
- LOCATE (SVC 26) for catalog lookup
- SCRATCH (SVC 29) for dataset deletion
- RENAME via CATALOG management

Much of this low-level I/O is available through crent370's `os/` module (dynamic allocation wrappers, dataset open/close) and `emfile/` module.

### 4.2 Dataset Catalog — Abstracted via Provider Interface

**Design principle:** The dataset catalog is accessed through an abstract **catalog provider interface**. The initial implementation uses VTOC scanning, but the interface is designed so that alternative backends (e.g. a future catalog service, or direct CVOL/VSAM catalog access) can be swapped in without changing the FTP logic.

**Catalog provider interface (conceptual):**
```c
typedef struct ftpd_catprov {
    int  (*list)(const char *pattern, ftpd_dsinfo_t *results, int max);
    int  (*stat)(const char *dsname, ftpd_dsinfo_t *info);
    void (*invalidate)(const char *dsname);  /* after STOR/DELE */
    void (*close)(void);
} ftpd_catprov_t;
```

**z/OS-inspired behavior:**
- `CWD` only sets the dataset name prefix — no I/O, no scan
- `LIST` triggers a filtered VTOC scan across configured volumes, matching against the current prefix. Results are cached per-session.
- `RETR` / `STOR` / `DELE` use OBTAIN (SVC 27) for individual dataset lookups
- After `STOR` or `DELE`, the session cache is invalidated for the affected dataset
- No global startup scan — the server starts immediately
- No `/F FTPD,REFRESH` needed (but could be kept for manual full-cache clear)

**VTOC scan provider (initial implementation):**
- Scans Format-1 DSCBs on all configured DASD volumes
- Filters by dataset name pattern during scan (skip non-matching DSCBs early)
- Returns dataset attributes: DSNAME, DSORG, RECFM, LRECL, BLKSIZE, volume, extents
- Per-session result cache: cleared on CWD to a different prefix, or on STOR/DELE

**Future alternatives:**
- Direct CVOL/VSAM catalog lookup via LOCATE (SVC 26) + OBTAIN for attributes
- A shared catalog service (cross-address-space, like UFSD)
- Hybrid: catalog for name resolution, VTOC only for attributes

### 4.3 JES2 Interface

JES2 integration leverages crent370's `jes/` module and the internal reader DD:

**Job Submission:**
1. Open internal reader programmatically via crent370's `jes/` module
2. Write JCL records to internal reader
3. Parse JES2 response messages for job number
4. Return: `250-It is known to JES as JOBnnnnn`

**Job Status Query:**
- Use crent370's `jes/` module for job status queries
- Return job list in z/OS-compatible format

**Spool File Retrieval:**
- Use crent370's `jes/` module for spool dataset access
- Read and return spool content

### 4.4 UFS Integration via UFSD

**UFSD** (mvslovers/ufsd) is a Cross-Address-Space Filesystem Server. It runs as its own started task and provides filesystem services to client programs in other address spaces.

Our FTPD links against the **UFSD client library** and communicates with the UFSD server via cross-memory services (PC routines or similar IPC mechanism from crent370's `ipc/` module).

Operations via UFSD client:
- `ufs_open()`, `ufs_read()`, `ufs_write()`, `ufs_close()`
- `ufs_stat()`, `ufs_opendir()`, `ufs_readdir()`
- `ufs_mkdir()`, `ufs_rmdir()`, `ufs_unlink()`
- `ufs_rename()`

If UFSD is not running, UFS commands return `550 UFS service not available`.

### 4.5 Authentication

Authentication uses crent370's `racf/` module (which integrates with RAKF on MVS/CE):

1. `USER userid` → save userid
2. `PASS password` → verify via RAKF (SVC 244)
3. Check FACILITY class resource `FTPAUTH` for authorization
4. On success: set HLQ = userid, session ready

---

## 5. Configuration

### 5.1 Configuration File

Read via `DD:FTPDPRM` in the STC JCL procedure. The DD card points to
the configuration member (default `SYS2.PARMLIB(FTPDPRM0)`), allowing
operators to override the location via PROC parameters without changing
the program.

```
# FTPD Configuration File
# ========================

# Network
SRVPORT=21
SRVIP=ANY
PASVADR=127,0,0,1
PASVPORTS=22000-22200
INSECURE=0

# Limits
MAXSESSIONS=10
IDLETIMEOUT=300
BANNER=MVS 3.8j FTPD Server

# Security
AUTHUSER=IBMUSER

# JES
JESINTERFACELEVEL=2

# Default dataset allocation for new datasets (STOR)
DEFRECFM=FB
DEFLRECL=80
DEFBLKSIZE=3120
DEFUNIT=3390
DEFVOLUME=PUB001

# DASD volumes for VTOC scanning
MVSRES,3350   SYSTEM RESIDENCE
MVS000,3350   SYSTEM DATASETS
PUB000,3380   PUBLIC DATASETS
PUB001,3390   PUBLIC DATASETS
SYSCPK,3350   COMPILER/TOOLS
```

### 5.2 JCL Procedure

```jcl
//FTPD    PROC
//*********************************************************************
//*  FTPD - FTP Server for MVS 3.8j
//*  Start: /S FTPD       Stop: /P FTPD
//*********************************************************************
//FTPD    EXEC PGM=FTPD,TIME=1440,REGION=8192K
//STEPLIB  DD  DSN=SYS2.LINKLIB,DISP=SHR
//STDOUT   DD  SYSOUT=*
//STDERR   DD  SYSOUT=*
```

### 5.3 Console Commands

| Command | Description |
|---------|-------------|
| `/S FTPD` | Start FTP server |
| `/P FTPD` | Stop FTP server (graceful shutdown) |
| `/F FTPD,D SESSIONS` | Display active sessions |
| `/F FTPD,D STATS` | Display server statistics |
| `/F FTPD,D VERSION` | Display version info |
| `/F FTPD,D CONFIG` | Display active configuration |
| `/F FTPD,KILL sessid` | Force-disconnect a session |
| `/F FTPD,TRACE ON` | Enable trace ring buffer |
| `/F FTPD,TRACE OFF` | Disable trace ring buffer |
| `/F FTPD,TRACE DUMP` | Dump trace buffer to STDOUT (WTO confirms with entry count) |

---

## 6. Build System

### 6.1 Toolchain — mbt (MVS Build Tool)

The project uses **mbt** (mvslovers/mbt) — the standard build tool for mvslovers projects:

```
project.toml   ──► mbt orchestrates the full pipeline:
                   C source (.c)
                     └─► c2asm370     (cross-compile to S/370 asm on host)
                           └─► IFOX00      (assemble on MVS via mvsMF API)
                                 └─► IEWL (NCAL)    (link to NCALIB)
                                       └─► IEWL (final)   (build FTPD load module)
```

**2-line Makefile:**
```makefile
MBT_ROOT := mbt
include $(MBT_ROOT)/mk/core.mk
```

**Build commands:**
```bash
make doctor         # Verify environment (Python, c2asm370, MVS connectivity)
make bootstrap      # Resolve deps (crent370, ufsd), upload to MVS, allocate datasets
make build          # Cross-compile + assemble (incremental)
make link           # Final linkedit → FTPD load module
make package        # Create release artifacts in dist/
```

### 6.2 Dependencies

| Dependency | Version | Purpose |
|------------|---------|---------|
| `mvslovers/crent370` | `>=1.0.0` | C runtime: sockets, threads (thdmgr), JES, RACF, IPC, OS, memory |
| `mvslovers/ufsd` | `>=0.1.0` | UFSD client library for UFS filesystem access |

These are resolved automatically by mbt via GitHub Releases. Lockfile (`.mbt/mvs.lock`) ensures reproducible builds.

### 6.3 Load Modules

| Module | Purpose |
|--------|---------|
| `FTPD` | Main FTP server daemon (standalone load module) |

---

## 7. Implementation Plan

### Phase 1: Foundation (Core FTP + MVS Datasets)

**Goal:** Basic FTP server that can transfer MVS datasets.

**Step 1.1 — Project scaffolding**
- Initialize repo with mbt submodule, `project.toml`, `.env.example`
- `make doctor` + `make bootstrap` work
- Implement `ftpdcfg.c` (parse FTPDPM00 config file)
- Implement `ftpdlog.c` (WTO messages + STDOUT logging)

**Step 1.2 — Network layer**
- `ftpd.c` — Main listener: socket, bind, listen, accept loop via crent370 socket API
- `ftpdses.c` — Session state machine + thread lifecycle (thdmgr)
- `ftpddata.c` — PORT/PASV data connection setup and teardown
- `ftpdxlat.c` — EBCDIC ↔ ASCII translation tables (IBM-1047)

**Step 1.3 — Command processing**
- `ftpdcmd.c` — Command parser: read line from control socket, tokenize, dispatch
- `ftpdauth.c` — USER/PASS via crent370 racf module
- Basic commands: SYST, TYPE, MODE, STRU, NOOP, QUIT, HELP, FEAT, STAT

**Step 1.4 — MVS dataset access**
- `ftpdmvs.c` — VTOC scanning, OBTAIN, dynamic allocation, OPEN/READ/WRITE/CLOSE
- `ftpdlist.c` — z/OS-compatible LIST formatting for datasets + PDS members
- CWD/PWD for MVS, LIST/NLST, RETR, STOR, DELE, RNFR/RNTO

**Step 1.5 — SITE command framework**
- `ftpdsite.c` — SITE command dispatcher
- Dataset allocation parameters (RECFM, LRECL, BLKSIZE, etc.)
- 502 responses for unimplemented z/OS SITE subcommands

**Deliverable:** FTP server that works with FileZilla, command-line ftp, and standard clients for MVS dataset transfers.

### Phase 2: JES Interface

**Goal:** Submit jobs and retrieve spool output via FTP.

**Step 2.1 — Job submission**
- `SITE FILETYPE=JES` mode switch in ftpdsite.c
- `ftpdjes.c` — STOR writes to internal reader DD, extracts job number

**Step 2.2 — Job query**
- LIST in JES mode → job queue listing
- JESINTERFACELEVEL 1 + 2, JESOWNER/JESJOBNAME/JESSTATUS filters

**Step 2.3 — Spool retrieval**
- RETR JOBnnnnn / RETR JOBnnnnn.n
- DELE JOBnnnnn (purge job)

**Deliverable:** Full JES interface, compatible with Zowe CLI FTP plugin and zos-node-accessor.

### Phase 3: UFS Support via UFSD

**Goal:** Seamless UFS file access alongside MVS datasets.

**Step 3.1 — UFSD client integration**
- `ftpdufs.c` — Wrapper around UFSD client library
- Auto-detect UFSD availability at session start
- `550 UFS service not available` if UFSD not running

**Step 3.2 — UFS operations**
- CWD/PWD in UFS mode
- LIST (Unix-style), RETR/STOR/DELE/RNFR/RNTO for files
- MKD/RMD for directories

**Step 3.3 — Hybrid navigation**
- Seamless switch between MVS and UFS within a session
- `CWD /` → UFS, `CWD 'HLQ.'` → MVS

**Deliverable:** Full dual-filesystem FTP server.

### Phase 4: Polish & Hardening

**Goal:** Production quality.

1. Console commands (/F FTPD,D SESSIONS, etc.)
2. Connection limits (max sessions, per-user limits)
3. Idle timeout (automatic disconnection)
4. REST command (transfer restart)
5. Error handling (proper FTP reply codes for all conditions)
6. Performance (VTOC caching, buffer tuning)
7. Documentation (README, admin guide)
8. MVP package (`RX MVP INSTALL FTPD`)
9. CI/CD via mbt GitHub Actions workflows

### Phase 5: SITE XMIT Support

**Goal:** Download/upload entire datasets (including PDS with all members) in TSO TRANSMIT format.

`SITE XMIT` switches the server into a mode where RETR produces XMIT-format output and STOR accepts XMIT-format input. This preserves all dataset attributes (RECFM, LRECL, BLKSIZE, DSORG) and allows transferring complete PDS libraries as a single file.

**Dependency:** Requires a TRANSMIT/RECEIVE library. Options:
- Use NJE38's TRANSMIT/RECEIVE if available (soft dependency, like UFSD)
- Future: standalone C library for XMIT format (separate mvslovers project)

**Use cases:**
- Backup/restore of PDS libraries via FTP
- Software distribution (XMIT is the standard MVS package format)
- Cross-system dataset migration with full attribute preservation
- Compatibility with existing `.XMI` files in the MVS/CE ecosystem

---

## 8. Client Compatibility

The server should work with:
- Command-line FTP clients (Windows `ftp`, Linux `ftp`, `lftp`, `curl`)
- FileZilla and other GUI clients
- Zowe CLI z/OS FTP plugin (`zowe zftp`)
- zos-node-accessor (Node.js)
- FluentFTP (.NET)
- Any RFC 959-compliant client

**Key compatibility requirements:**
- `SYST` response format must match z/OS convention
- LIST output format must be parseable by automated tools
- SITE command syntax matches z/OS semantics
- Dataset name handling (qualified/unqualified, single quotes) follows z/OS rules
- JES interface behavior matches z/OS for submission + retrieval workflows

### MVS 3.8j vs z/OS Limitations

Some z/OS FTP features cannot exist on MVS 3.8j:
- SMS (Storage Management Subsystem) — doesn't exist
- PDSE — only PDS
- HFS/zFS — UFSD is the equivalent
- RACF — RAKF is the substitute
- USS — UFSD replaces this
- Modern JES2 SSI — may need alternative approaches
- TLS/FTPS — not available at the application level

---

## 9. Testing Strategy

### Unit Testing
- Command parser tests (ftpdcmd)
- EBCDIC ↔ ASCII translation verification
- Dataset name parsing (qualified, unqualified, PDS members, wildcards)
- SITE parameter parsing

### Integration Testing
- End-to-end transfers with standard FTP clients
- Dataset creation with various RECFM/LRECL combinations
- PDS member operations (list, read, write, delete)
- JES job submission and spool retrieval
- UFS file operations (requires UFSD running)
- Mixed MVS/UFS navigation in single session

### Compatibility Testing
- FileZilla, Windows ftp, Linux ftp, lftp, curl --ftp
- Zowe CLI z/OS FTP plugin
- zos-node-accessor (Node.js)
- Verify LIST format parsing by automated tools

---

## 10. Decisions Log

All major design questions have been resolved:

1. ~~**VTOC caching strategy**~~ → **Decided:** Per-session filtered VTOC scan behind abstract catalog provider interface. CWD sets prefix only, LIST triggers scan. No startup scan. See §4.2.

2. ~~**JES2 spool access**~~ → **Decided:** Use crent370's `jes/` module directly for job status queries and spool retrieval.

3. ~~**Internal reader DD**~~ → **Decided:** Use crent370's `jes/` module programmatically. No AAINTRDR DD in JCL.

4. ~~**UFSD dependency model**~~ → **Decided:** Soft dependency. FTPD works standalone for MVS datasets, returns `550 UFS service not available` if UFSD is not running.

5. ~~**Configuration format**~~ → **Decided:** Key=value format, compatible with existing FTPDPM00.

6. ~~**Dataset name wildcard matching**~~ → **Decided:** Support both `*` (any characters within a qualifier) and `%` (exactly one character), matching z/OS behavior.

7. ~~**Logging verbosity**~~ → **Decided:** WTO for important events (start, stop, auth failures, errors) and operator command responses (`/F FTPD,D ...`). STDOUT for everything else (transfer details, debug, session activity). Additionally: **trace ring buffer** (like UFSD) — enabled/disabled/dumped via `/F FTPD,TRACE ON|OFF|DUMP`. Dump writes to STDOUT, WTO only confirms with entry count.

### z/OS Protocol Reference Findings (from `ZOS_FTP_REFERENCE.md`)

The following behaviors were confirmed from z/OS 3.1 FTP server protocol captures and incorporated into the concept:

8. **SYST is mode-aware:** Returns `215 MVS` in dataset mode, `215 UNIX` in UFS mode. See §2.1.
9. **CWD does not verify datasets:** CWD is a pure prefix manipulation — always returns `250`, even for nonexistent datasets. See §2.3.
10. **Bare SITE returns `202`:** Not a settings dump. Use `STAT` for session settings. See §2.2.
11. **Unknown SITE params return `200` with warning:** Not `502`. Lenient for forward compatibility. See §2.2.
12. **TYPE E record separator is X'15' (NL):** Not CRLF, not X'25'. See §2.6.
13. **TRAILING default: blanks NOT removed.** Matches z/OS default. See §2.2.
14. **REST requires MODE B or C:** `504` in MODE S (Stream). REST STREAM removed from FEAT. See Appendix C.
15. **Pre-auth commands allowed:** SYST, FEAT, HELP, NOOP, STAT, SITE, QUIT work before USER/PASS. See §2.6.
16. **APPE creates nonexistent datasets.** See §2.3.
17. **MKD in MVS mode allocates PDS.** RMD scratches PDS (same as DELE). See §2.3.
18. **RNFR state cleared on any non-RNTO command.** Stricter than z/OS (which has a stale-RNFR bug). See §2.3.
19. **Spool DD separator:** ` !! END OF JES SPOOL FILE !!` (space-prefixed, exact string). DD numbering starts at 1. See §2.5.
20. **DELE JOB response:** `250 Cancel successful` (matches z/OS). See §2.5.

---

## Appendix A: FTP Reply Codes

| Code | Meaning |
|------|---------|
| 125 | Data connection already open, transfer starting |
| 150 | File status okay, about to open data connection |
| 200 | Command okay |
| 202 | Command superfluous (bare SITE) |
| 211 | System status / FEAT response |
| 213 | File status (SIZE response) |
| 214 | Help message |
| 215 | NAME system type |
| 220 | Service ready for new user |
| 221 | Service closing control connection |
| 226 | Closing data connection, transfer complete / Abort successful |
| 227 | Entering Passive Mode |
| 230 | User logged in |
| 250 | Requested file action okay |
| 257 | Pathname created/current directory |
| 331 | User name okay, need password |
| 350 | Requested file action pending further info |
| 421 | Service not available |
| 425 | Can't open data connection |
| 426 | Connection closed, transfer aborted |
| 450 | File unavailable (busy) |
| 451 | Requested action aborted, local error |
| 452 | Insufficient storage space |
| 500 | Syntax error, unrecognized command |
| 501 | Syntax error in parameters |
| 502 | Command not implemented |
| 504 | Command/parameter not implemented (MODE B/C, REST in stream mode) |
| 530 | Not logged in |
| 550 | File unavailable (not found, no access) |
| 552 | Exceeded storage allocation |
| 553 | File name not allowed |

## Appendix B: z/OS SITE Subcommands Reference

**Implemented (return `200 SITE command was accepted`):**
BLKSIZE, CYLINDERS, DIRECTORY, FILETYPE, JESINTERFACELEVEL,
JESJOBNAME, JESOWNER, JESSTATUS, LRECL, PRIMARY, RECFM,
RDW, SBSENDEOL, SECONDARY, TRACKS, TRAILING, TRUNCATE, UNIT, VOLUME,
XMIT (Phase 5)

**Accepted silently (return `200` — no effect on MVS 3.8j):**
DATACLAS, STORCLAS, MGMTCLAS (SMS classes — accepted for client compatibility)

**Unrecognized parameters (return `200` with warning):**
All other z/OS SITE subcommands are accepted with:
`200-Unrecognized parameter 'XXX' on SITE command.` / `200 SITE command was accepted`
This matches z/OS behavior and prevents client errors.

## Appendix C: FEAT Response

Note: The z/OS 3.1 test server returned `211 no Extensions supported` (with SIZE/MDTM disabled). We choose to advertise features per RFC 2389 for better client compatibility:

```
211-Features supported
 SIZE
 MDTM
 SITE FILETYPE
 SITE JES
 UTF8
211 End
```

`REST STREAM` is **not** advertised because z/OS requires MODE B or C for REST (returns `504 Restart requires Block or Compressed transfer mode` in MODE S). Since we only support MODE S in Phase 1, REST is not available. REST support may be added when MODE B is implemented.
