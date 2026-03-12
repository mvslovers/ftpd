# FTPD Implementation Plan — Phase 1: Foundation

**Phase:** 1 of 5  
**Goal:** Basic FTP server that can list, upload, download, and delete MVS datasets.  
**Prerequisite:** Concept document `FTPD_CONCEPT.md`  
**Deliverable:** A working FTP server that passes all acceptance criteria below.

---

## Environment Notes for Claude Code

- **All mvslovers projects are in the same parent directory.** To understand crent370 APIs, look at `../crent370/include/` headers. For UFSD client APIs, look at `../ufsd/client/` and `../ufsd/include/`. For HTTPD as reference (threading, sockets, EBCDIC), look at `../httpd/src/` and `../httpd/include/`.
- **Build system is mbt.** The project uses a 2-line Makefile and `project.toml`. See `../mbt/` for build tool internals, `../mbt/examples/hello370/` for a minimal example project.
- **Do NOT invent APIs.** Always check the actual crent370 headers before using any runtime function. If a function doesn't exist, flag it — don't guess.
- **All code is C (c2asm370 compatible).** This is GCC 3.2.3 targeting S/370. No C99+ features, no stdint.h, no variadic macros. Keep it simple.
- **All strings are EBCDIC internally.** ASCII only on the wire (network I/O).
- **Comments and documentation in English.**

---

## Step 1.1 — Project Scaffolding

### What
Initialize the repository structure, build configuration, and basic infrastructure modules (config parsing, logging).

### Files to Create

| File | Purpose |
|------|---------|
| `project.toml` | mbt project definition (see concept §3.3) |
| `Makefile` | 2-line Makefile: `MBT_ROOT := mbt` + `include $(MBT_ROOT)/mk/core.mk` |
| `.env.example` | Example MVS connection config |
| `.gitignore` | Ignore `.env`, `asm/*.s`, `contrib/`, `dist/`, `.mbt/` |
| `.gitmodules` | mbt submodule reference |
| `src/ftpdcfg.c` | Configuration file parser |
| `src/ftpdlog.c` | Logging module (WTO + STDOUT + trace ring buffer) |
| `include/ftpd.h` | Main header: shared constants, structures, forward declarations |
| `include/ftpdcfg.h` | Config structures and function prototypes |
| `include/ftpdlog.h` | Logging macros and prototypes |
| `jcl/FTPD.jcl` | STC procedure |

### Implementation Details

**ftpdcfg.c — Configuration Parser:**
- Read `SYS1.PARMLIB(FTPDPM00)` (or PARM-specified dataset)
- Parse key=value format, `#` comments, DASD volume lines
- Populate `ftpd_config_t` structure (see concept §3.5)
- Default values for all parameters
- Validation: port range, IP format, PASV port range

**ftpdlog.c — Logging:**
- `ftpd_log_wto(msg)` — Write to operator (WTO macro) for important events
- `ftpd_log(level, fmt, ...)` — Write to STDOUT with timestamp and level
- Trace ring buffer: fixed-size circular buffer in memory
- `ftpd_trace(fmt, ...)` — Write to ring buffer (when tracing enabled)
- `ftpd_trace_dump()` — Dump ring buffer contents to STDOUT
- Log levels: ERROR, WARN, INFO, DEBUG

**ftpd.h — Main Header:**
- `ftpd_config_t` structure
- `ftpd_session_t` structure (forward declare, full definition in ftpdses.h)
- Constants: `FT_SEQ`, `FT_JES`, `FS_MVS`, `FS_UFS`
- Session states: `SESS_GREETING`, `SESS_AUTH_USER`, etc.
- FTP reply code constants
- Global server state pointer

### Dependencies
- mbt submodule must be added and `make doctor` must pass
- crent370 must be resolvable via `make bootstrap`

### Acceptance Criteria
- [ ] `make doctor` passes — c2asm370 found, MVS reachable
- [ ] `make bootstrap` completes — crent370 + ufsd resolved, datasets allocated on MVS
- [ ] `make build` compiles `ftpdcfg.c` and `ftpdlog.c` without errors
- [ ] Config parser correctly reads the example FTPDPM00 config (unit test or manual verification)
- [ ] Logging produces WTO messages and STDOUT output
- [ ] Trace ring buffer can be enabled, written to, and dumped

---

## Step 1.2 — Network Layer

### What
Main listener loop, session lifecycle management, data connection handling, and EBCDIC/ASCII translation.

### Files to Create

| File | Purpose |
|------|---------|
| `src/ftpd.c` | Main entry: socket listener, accept loop, console command handler, shutdown |
| `src/ftpdses.c` | Session state machine, thread lifecycle via thdmgr |
| `src/ftpddata.c` | Data connection management (PORT/PASV) |
| `src/ftpdxlat.c` | EBCDIC ↔ ASCII translation tables (IBM-1047) |
| `include/ftpdses.h` | Session state definitions, session structure |
| `include/ftpdxlat.h` | Translation table declarations |

### Implementation Details

**ftpd.c — Main Entry Point:**
- Parse PARM field for config overrides
- Call `ftpdcfg_load()` to read config
- Initialize logging
- Create listening socket (crent370 socket API)
- Bind to configured address:port
- Accept loop: for each connection, spawn session thread via thdmgr
- Console command handler (MODIFY commands via crent370 COMM interface):
  - `D SESSIONS` — list active sessions (WTO response)
  - `D STATS` — show transfer statistics (WTO response)
  - `D VERSION` — show version string (WTO response)
  - `D CONFIG` — show active config (WTO response)
  - `KILL sessid` — force-close a session
  - `TRACE ON|OFF|DUMP` — control trace ring buffer
- Graceful shutdown on STOP command: close listener, wait for sessions, exit

**ftpdses.c — Session Handler:**
- `ftpd_session_new(sock)` — Allocate and initialize session context
- `ftpd_session_run(sess)` — Main session loop (thread entry point):
  1. Send 220 greeting
  2. Read command line from control socket
  3. Translate ASCII → EBCDIC
  4. Parse and dispatch (calls into ftpdcmd.c — Step 1.3)
  5. Loop until QUIT or error
- `ftpd_session_free(sess)` — Cleanup: close sockets, free memory
- `ftpd_session_reply(sess, code, msg)` — Send FTP reply (EBCDIC → ASCII)
- Track active sessions in a linked list or array for console display

**ftpddata.c — Data Connections:**
- `ftpd_data_port(sess, h1,h2,h3,h4,p1,p2)` — Parse PORT command, store target
- `ftpd_data_pasv(sess)` — Open passive listener on port from configured range, return 227 response
- `ftpd_data_open(sess)` — Establish data connection (connect for active, accept for passive)
- `ftpd_data_close(sess)` — Close data connection socket
- `ftpd_data_send(sess, buf, len)` — Write to data connection
- `ftpd_data_recv(sess, buf, len)` — Read from data connection

**ftpdxlat.c — Translation:**
- `ascii_to_ebcdic[256]` — Static lookup table (IBM-1047 ↔ ISO-8859-1)
- `ebcdic_to_ascii[256]` — Static lookup table
- `ftpd_xlat_a2e(buf, len)` — In-place ASCII → EBCDIC
- `ftpd_xlat_e2a(buf, len)` — In-place EBCDIC → ASCII
- Reference: use the same tables as HTTPD (`../httpd/src/` for reference)

### Dependencies
- Step 1.1 (config, logging, headers)
- crent370: socket API, thdmgr, COMM interface

### Acceptance Criteria
- [ ] Server starts as STC (`/S FTPD`), binds to configured port, logs startup via WTO
- [ ] Server shuts down cleanly on `/P FTPD`
- [ ] FTP client can connect and receives `220` greeting
- [ ] Multiple concurrent connections are handled (each in own thdmgr thread)
- [ ] `/F FTPD,D VERSION` responds with version string on console
- [ ] `/F FTPD,TRACE ON` / `TRACE OFF` / `TRACE DUMP` work
- [ ] PASV mode: client can establish passive data connection
- [ ] PORT mode: server can connect back to client for data transfer
- [ ] EBCDIC ↔ ASCII translation is correct for all printable characters

---

## Step 1.3 — Command Processing

### What
FTP command parser, dispatcher, authentication, and basic commands that don't require dataset access.

### Files to Create

| File | Purpose |
|------|---------|
| `src/ftpdcmd.c` | Command parser: read line, tokenize, dispatch to handler |
| `src/ftpdauth.c` | Authentication: USER/PASS via crent370 racf module |

### Implementation Details

**ftpdcmd.c — Command Parser & Dispatcher:**
- `ftpd_cmd_parse(line, cmd, arg)` — Split input line into command + argument
- Command dispatch table: array of `{ "XXXX", handler_func }` entries
- Case-insensitive command matching (FTP commands are case-insensitive)
- Unknown commands → `500 Syntax error, unrecognized command`
- Commands requiring authentication check `sess->authenticated` first → `530 Not logged in`

**Basic commands (no dataset I/O):**
- `SYST` → `215 MVS is the operating system`
- `TYPE A|E|I` → Set transfer type, `200 Type set to X`
- `MODE S` → `200 Mode set to S` (only Stream supported)
- `STRU F|R` → Set structure, `200 Structure set to X`
- `NOOP` → `200 Command okay`
- `QUIT` → `221 Goodbye`, close session
- `HELP` → `214-` multi-line help text listing available commands
- `FEAT` → `211-Features supported` / `SIZE` / `MDTM` / `REST STREAM` / `SITE FILETYPE` / `SITE JES` / `UTF8` / `211 End`
- `STAT` → `211-` server status (version, session info)
- `ABOR` → `226 Abort successful` (close any pending data connection)
- `PORT` → delegate to `ftpd_data_port()`
- `PASV` → delegate to `ftpd_data_pasv()`

**ftpdauth.c — Authentication:**
- `USER name` → Save username in session, respond `331 User name okay, need password`
- `PASS password` → Verify via crent370 `racf/` module (RAKF SVC 244)
  - Check FACILITY class resource `FTPAUTH`
  - On success: set `sess->authenticated = 1`, `sess->hlq = userid`, respond `230 User logged in`
  - On failure: increment attempt counter, respond `530 Login incorrect`
  - After 3 failures: close connection
- Reference: check `../httpd/src/` and `../httpd/credentials/` for how HTTPD handles auth

### Dependencies
- Step 1.2 (session handler, reply function, data connection)
- crent370: `racf/` module for authentication

### Acceptance Criteria
- [ ] FTP client can complete `USER` / `PASS` login handshake
- [ ] Invalid credentials are rejected with `530`
- [ ] After 3 failed attempts, connection is closed
- [ ] `SYST` returns `215 MVS is the operating system`
- [ ] `TYPE A`, `TYPE I`, `TYPE E` all work and are reflected in session state
- [ ] `FEAT` returns the correct feature list
- [ ] `PASV` + `LIST` sequence works (LIST will return empty for now — dataset access is Step 1.4)
- [ ] `QUIT` cleanly closes the connection
- [ ] Unknown commands return `500`
- [ ] Commands before authentication return `530`

---

## Step 1.4 — MVS Dataset Access

### What
VTOC scanning, dataset catalog provider, dataset I/O (read/write), and LIST formatting. This is the core of Phase 1.

### Files to Create

| File | Purpose |
|------|---------|
| `src/ftpdmvs.c` | MVS dataset operations: catalog provider, OBTAIN, dynamic allocation, OPEN/READ/WRITE/CLOSE |
| `src/ftpdlist.c` | LIST/NLST formatting for MVS datasets and PDS members |
| `include/ftpdmvs.h` | Catalog provider interface, dataset info structures |

### Implementation Details

**Catalog Provider Interface (ftpdmvs.h):**
```c
/* Dataset info returned by catalog operations */
typedef struct ftpd_dsinfo {
    char    dsname[45];     /* Dataset name                  */
    char    volser[7];      /* Volume serial                 */
    char    unit[5];        /* Device type                   */
    char    dsorg[3];       /* PS, PO, DA                    */
    char    recfm[4];       /* FB, VB, F, V, U, etc.         */
    int     lrecl;          /* Logical record length          */
    int     blksize;        /* Block size                     */
    int     extents;        /* Number of extents              */
    int     used_tracks;    /* Used tracks                    */
    /* TODO: referred date, creation date if available */
} ftpd_dsinfo_t;

/* Abstract catalog provider */
typedef struct ftpd_catprov {
    int  (*list)(const char *pattern, ftpd_dsinfo_t *results, int max);
    int  (*stat)(const char *dsname, ftpd_dsinfo_t *info);
    void (*invalidate)(const char *dsname);
    void (*close)(void);
} ftpd_catprov_t;
```

**ftpdmvs.c — Dataset Operations:**

*Catalog provider (VTOC scan implementation):*
- `ftpd_vtoc_list(pattern, results, max)` — Scan VTOC on all configured volumes, filter by pattern. Supports `*` (any chars within qualifier) and `%` (single char).
- `ftpd_vtoc_stat(dsname, info)` — OBTAIN (SVC 27) for single dataset
- `ftpd_vtoc_invalidate(dsname)` — Remove entry from per-session cache
- Per-session cache: results of last `list()` call, invalidated on CWD prefix change or STOR/DELE

*Dataset name handling:*
- `ftpd_dsn_qualify(sess, input, output)` — Apply HLQ prefix rules:
  - `'SYS1.MACLIB'` → `SYS1.MACLIB` (quoted = fully qualified, strip quotes)
  - `MYDATA` → `USERID.MYDATA` (unqualified = prefix with HLQ)
  - `MYLIB(MEMBER)` → `USERID.MYLIB(MEMBER)` (member access)
- `ftpd_dsn_split_member(dsname, base, member)` — Separate `DSN(MBR)` into parts
- `ftpd_dsn_match(pattern, dsname)` — Wildcard matching (`*` and `%`)

*Dataset I/O:*
- `ftpd_mvs_read_seq(sess, dsname)` — Read sequential dataset, send to data connection
- `ftpd_mvs_read_member(sess, dsname, member)` — Read PDS member
- `ftpd_mvs_write_seq(sess, dsname)` — Receive from data connection, write sequential dataset
- `ftpd_mvs_write_member(sess, dsname, member)` — Write PDS member (STOW after close)
- `ftpd_mvs_delete(sess, dsname)` — SCRATCH (SVC 29)
- `ftpd_mvs_delete_member(sess, dsname, member)` — STOW DELETE
- `ftpd_mvs_rename(sess, old, new)` — CATALOG rename
- `ftpd_mvs_alloc_new(sess, dsname)` — Dynamic allocation for new dataset using SITE params
- Use crent370's `os/` module (dynamic allocation, OPEN/CLOSE) and `emfile/` where available
- Reference: check existing FTPD source at `../FTPD/source/` for VTOC scanning and I/O patterns

*FTP command handlers in ftpdmvs.c:*
- `CWD dsname` → Set `sess->mvs_cwd` prefix (no I/O)
- `CDUP` → Remove last qualifier from prefix
- `PWD` → Return `257 "'prefix'" is working directory`
- `RETR dsname` → Read dataset/member, send on data connection
- `STOR dsname` → Receive on data connection, write dataset/member
- `DELE dsname` → Delete dataset or member
- `RNFR dsname` → Store source name in `sess->rnfr_path`
- `RNTO dsname` → Rename from `rnfr_path` to new name
- `SIZE dsname` → Return dataset size (from OBTAIN)
- `APPE dsname` → Append to sequential dataset

**ftpdlist.c — LIST Formatting:**

*MVS dataset list (z/OS compatible):*
```
Volume Unit    Referred Ext Used Recfm Lrecl BlkSz Dsorg Dsname
PUB001 3390   2026/03/12  1   15  FB      80 27920  PO  SYS1.PARMLIB
```
- `ftpd_list_datasets(sess, pattern)` — Call catalog provider, format each entry
- Column widths must match z/OS format for client compatibility (FileZilla, Zowe, etc.)

*PDS member list:*
```
 Name     VV.MM  Created   Changed     Size  Init   Mod   Id
 IEASYS00 01.05 2024/01/15 2025/06/20  45    45     3     IBMUSER
```
- `ftpd_list_members(sess, dsname)` — Read PDS directory, format each member
- Include ISPF statistics if present in directory entry (userdata)

*NLST:*
- `ftpd_nlst(sess, pattern)` — Name-only list, one name per line

*Transfer:*
- Open data connection, send formatted listing, close data connection
- Respect TYPE A (ASCII translation on) or TYPE E (no translation)

### Dependencies
- Step 1.2 (data connections, translation)
- Step 1.3 (command dispatcher, authentication)
- crent370: `os/` module (dynamic allocation, dataset I/O), `emfile/` module

### Acceptance Criteria
- [ ] `CWD` sets the working prefix; `PWD` returns it in z/OS format: `257 "'IBMUSER.'" is working directory`
- [ ] `LIST` returns z/OS-compatible dataset listing for the current prefix
- [ ] `LIST` with wildcard pattern works: `LIST *.LOAD` filters correctly
- [ ] `LIST` on a PDS returns member listing with ISPF stats
- [ ] `RETR` downloads a sequential dataset (ASCII mode: EBCDIC→ASCII, correct line endings)
- [ ] `RETR` downloads a PDS member
- [ ] `RETR` in binary mode (TYPE I) transfers bytes unchanged
- [ ] `STOR` uploads to an existing sequential dataset
- [ ] `STOR` creates a new dataset using default SITE allocation parameters
- [ ] `STOR` writes a PDS member
- [ ] `DELE` deletes a sequential dataset
- [ ] `DELE` deletes a PDS member
- [ ] `RNFR`/`RNTO` renames a dataset
- [ ] Unqualified names are correctly prefixed with user HLQ
- [ ] Quoted names (`'SYS1.MACLIB'`) are treated as fully qualified
- [ ] `%` wildcard matches exactly one character in LIST
- [ ] `*` wildcard matches any characters within one qualifier in LIST
- [ ] Cache is invalidated after STOR/DELE (subsequent LIST reflects changes)
- [ ] FileZilla can connect, browse, upload, and download (manual test)

---

## Step 1.5 — SITE Command Framework

### What
SITE command dispatcher and dataset allocation parameter handling. Also 502 responses for unimplemented z/OS SITE subcommands.

### Files to Create

| File | Purpose |
|------|---------|
| `src/ftpdsite.c` | SITE command parser and handlers |

### Implementation Details

**ftpdsite.c — SITE Commands:**

*Parser:*
- `ftpd_site_dispatch(sess, args)` — Parse SITE subcommand, dispatch to handler
- Case-insensitive subcommand matching
- Unrecognized subcommands → `500 SITE subcommand not recognized`

*Implemented subcommands (set session state):*
- `SITE FILETYPE=SEQ` → `sess->filetype = FT_SEQ` (default)
- `SITE FILETYPE=JES` → `sess->filetype = FT_JES` (Phase 2 — for now return `200` but JES commands are not yet available)
- `SITE RECFM=xx` → `sess->alloc.recfm = xx`
- `SITE LRECL=nnn` → `sess->alloc.lrecl = nnn`
- `SITE BLKSIZE=nnn` → `sess->alloc.blksize = nnn`
- `SITE PRIMARY=nnn` → `sess->alloc.primary = nnn`
- `SITE SECONDARY=nnn` → `sess->alloc.secondary = nnn`
- `SITE TRACKS` → `sess->alloc.spacetype = "TRK"`
- `SITE CYLINDERS` → `sess->alloc.spacetype = "CYL"`
- `SITE VOLUME=xxxxxx` → `sess->alloc.volume = xxxxxx`
- `SITE UNIT=xxxx` → `sess->alloc.unit = xxxx`
- `SITE DIRECTORY=nn` → `sess->alloc.dirblks = nn`
- `SITE TRAILING` → Set trailing blanks flag
- `SITE TRUNCATE` → Set truncate flag
- `SITE RDW` → Set RDW flag
- `SITE SBSENDEOL=CRLF|LF` → Set EOL mode
- All respond `200 SITE command was accepted`

*JES parameters (accepted, stored for Phase 2):*
- `SITE JESINTERFACELEVEL=n` → `sess->jes_level = n`
- `SITE JESJOBNAME=name` → `sess->jes_jobname = name`
- `SITE JESOWNER=owner` → `sess->jes_owner = owner`
- `SITE JESSTATUS=status` → `sess->jes_status = status`

*Recognized but not implemented (502):*
- BLOCKS, CONDDISP, DATACLAS, EATTR, ISPFSTATS, MGMTCLAS, NCP, QUOTESOVERRIDE, RETPD, SBDATACONN, SBSUB, SPACETYPE, STORCLAS, UCSMSG, WRAPAROUND
- All respond `502 SITE subcommand not implemented on this system`

*SITE with no arguments:*
- Return current SITE settings: RECFM, LRECL, BLKSIZE, etc.

### Dependencies
- Step 1.3 (command dispatcher)
- Step 1.4 (allocation parameters used by STOR)

### Acceptance Criteria
- [ ] `SITE RECFM=VB` sets the session allocation RECFM; confirmed by `SITE` (no args) displaying it
- [ ] `SITE LRECL=255` + `SITE BLKSIZE=6233` + `SITE RECFM=VB` → subsequent `STOR` creates dataset with these attributes
- [ ] `SITE FILETYPE=JES` returns `200` (JES mode set, but no JES commands available yet)
- [ ] `SITE FILETYPE=SEQ` switches back to sequential mode
- [ ] `SITE DATACLAS=xxx` returns `502 SITE subcommand not implemented on this system`
- [ ] Unknown `SITE FOOBAR` returns `500 SITE subcommand not recognized`
- [ ] `SITE` with no arguments displays current settings
- [ ] Zowe CLI `zowe zftp` can set SITE parameters without errors (manual test)

---

## Phase 1 Completion Criteria

When all steps pass their acceptance criteria, Phase 1 is complete. The overall test is:

1. **Start:** `/S FTPD` starts the server, `+FTP001I` startup messages appear on console
2. **Connect:** `ftp localhost 21` (or configured port) → `220 MVS 3.8j FTPD Server`
3. **Login:** `USER IBMUSER` → `331` → `PASS xxx` → `230 User logged in`
4. **Browse:** `PWD` → `257 "'IBMUSER.'"` → `LIST` → dataset listing
5. **Upload:** `SITE RECFM=FB` → `SITE LRECL=80` → `PUT local.txt 'IBMUSER.TEST.DATA'` → `226 Transfer complete`
6. **Download:** `GET 'IBMUSER.TEST.DATA' local2.txt` → `226 Transfer complete` → files match
7. **PDS:** `LIST 'SYS1.PARMLIB'` → member listing → `GET 'SYS1.PARMLIB(IEASYS00)'` → contents correct
8. **Delete:** `DELE 'IBMUSER.TEST.DATA'` → `250 Dataset deleted`
9. **Disconnect:** `QUIT` → `221 Goodbye`
10. **Stop:** `/P FTPD` → graceful shutdown, all sessions closed
11. **Clients:** FileZilla can connect, browse, upload, download (manual test)

---

## Notes

- **Incremental development:** Each step can be built and tested independently. Steps 1.1 → 1.2 → 1.3 must be done in order. Steps 1.4 and 1.5 can overlap.
- **Error handling:** Every dataset operation can fail. Always return appropriate FTP reply codes (450, 451, 452, 550, 552, 553). Never crash on bad input.
- **Memory management:** Sessions must clean up all resources on disconnect. No memory leaks across sessions.
- **Thread safety:** Session state is per-thread. Global state (config, active session list) needs protection if accessed from multiple threads.
