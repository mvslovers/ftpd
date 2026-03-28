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

## Step 1.1 — Project Scaffolding ✅ DONE

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
- Read config from `DD:FTPDPRM`
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

## Step 1.2 — Network Layer ✅ DONE

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

## Step 1.3 — Command Processing ✅ DONE

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
- Unknown commands → `500 unknown command XXXX`
- Commands requiring authentication check `sess->authenticated` first → `530 Not logged in.`
- **Pre-auth allowlist:** SYST, FEAT, HELP, NOOP, STAT, SITE, QUIT, USER, PASS are allowed before login. All others require authentication.

**Basic commands (no dataset I/O):**
- `SYST` → Mode-aware: `215 MVS is the operating system of this server. FTP Server is running on MVS.` (dataset mode) or `215 UNIX is the operating system of this server. FTP Server is running on MVS.` (UFS mode)
- `TYPE A|E|I` → Set transfer type, `200 Representation type is Ascii NonPrint` / `Image` / `Ebcdic NonPrint`
- `TYPE A N` / `TYPE E N` → Explicit NonPrint format, same response
- `TYPE L 8` → Accept as synonym for TYPE I: `200 Local byte 8, representation type is Image`
- `TYPE X` (unknown) → `501-unknown type  X` / `501 Type remains <current>` (multiline)
- `MODE S` → `200 Data transfer mode is Stream` (only Stream supported)
- `MODE B|C` → `504 MODE B/C not implemented` (z/OS accepts both, we don't for Phase 1)
- `STRU F|R` → `250 Data structure is File` / `Record`
- `STRU P` → `504-Page structure not implemented` / `504 Data structure remains <current>`
- `NOOP` → `200 OK`
- `QUIT` → `221 Quit command received. Goodbye.`
- `HELP` → `214-` multi-line listing of all supported commands (mark unimplemented with `*`)
- `FEAT` → `211-Features supported` / `SIZE` / `MDTM` / `SITE FILETYPE` / `SITE JES` / `UTF8` / `211 End`
- `STAT` → `211-` server status (connection info, current TYPE/MODE/STRU, SITE settings, JES config)
- `ABOR` → `226 Abort successful.` (even when no transfer is active)
- `PORT` → delegate to `ftpd_data_port()`
- `PASV` → delegate to `ftpd_data_pasv()`

**ftpdauth.c — Authentication:**
- `USER name` → Save username in session, respond `331 Send password please.` (same response regardless of whether userid exists — no user enumeration)
- `PASS password` → Verify via crent370 `racf/` module (RAKF SVC 244)
  - Check FACILITY class resource `FTPAUTH`
  - On success: set `sess->authenticated = 1`, `sess->hlq = userid`, respond `230-<userid> is logged on.  Working directory is "<userid>.".` / `230 <userid> is logged on.  Working directory is "<userid>.".`
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
- [ ] `SYST` returns `215 MVS is the operating system of this server. FTP Server is running on MVS.`
- [ ] `TYPE A`, `TYPE I`, `TYPE E`, `TYPE L 8` all work and are reflected in session state
- [ ] `TYPE X` returns multiline 501 error with current type
- [ ] `FEAT` returns the correct feature list (no REST STREAM)
- [ ] `STAT` shows current session settings
- [ ] `PASV` + `LIST` sequence works (LIST will return empty for now — dataset access is Step 1.4)
- [ ] `QUIT` cleanly closes the connection
- [ ] Unknown commands return `500`
- [ ] Pre-auth commands (SYST, FEAT, HELP, NOOP, STAT, SITE, QUIT) work before login
- [ ] Data/navigation commands before authentication return `530 Not logged in.`

---

## Step 1.4 — MVS Dataset Access ✅ DONE

### What
Catalog-based dataset listing, dataset I/O (read/write), and LIST formatting. Uses crent370 catalog functions (`__listds()`, `__listpd()`, `__locate()`, `__dscbdv()`, `__dsalcf()`) instead of VTOC scanning. Dataset I/O via standard C stdio (`fopen`/`fread`/`fwrite`+`fflush`/`fclose`). This is the core of Phase 1.

### Files Created

| File | Purpose |
|------|---------|
| `src/ftpd#mvs.c` | MVS dataset operations: catalog queries, dataset I/O, CWD logic, PDS context |
| `src/ftpd#lst.c` | LIST/NLST formatting for MVS datasets and PDS members |
| `include/ftpd#mvs.h` | MVS operation prototypes |

### Implementation Details

**Required crent370 headers:**
```c
#include "cliblist.h"    /* DSLIST, PDSLIST, ISPFSTAT, LOADSTAT, __listds(), __listpd(), __fmtisp(), __fmtloa() */
#include "clibdscb.h"    /* DSCB, LOCWORK, __locate(), __dscbdv() */
#include "clibio.h"      /* __dsalcf(), __dsfree() */
```

**Catalog queries (using crent370):**
- `LIST` on prefix → `__listds(prefix, "NONVSAM VOLUME", filter)` — returns `DSLIST**` array. Free with `__freeds()`.
- `LIST` on PDS → `__listpd(dsname, filter)` — returns `PDSLIST**` array. Format with `__fmtisp()` (RECFM=F/V) or `__fmtloa()` (RECFM=U). Free with `__freepd()`.
- Single dataset lookup → `__locate(dsn, &locwork)` → volser. Then `__dscbdv(dsn, vol, &dscb)` for attributes.
- CWD PDS detection → `__dscbdv()` — check `dscb.dscb1.dsorg1 & DSGPO`. Only when CWD arg has NO trailing dot.
- New dataset allocation → `__dsalcf(ddname, opts)` + `__dsfree(ddname)`. Then `fopen("'DSN'", "wb")`.
- No cache — `__listds()` queries the catalog directly each time.

**Dataset I/O (validated via mvsMF DSAPI pattern):**
- RETR: `fopen("'DSN'", "rb")` → `fread(buf, 1, lrecl, fp)` per record → `send()` → `fclose()`
- STOR: `fopen("'DSN'", "wb")` → `recv()` into record_buffer (limited to `lrecl - recpos`) → `fwrite(recbuf, 1, lrecl, fp)` + `fflush(fp)` per record → `fclose()`
- `fclose()` properly releases dataset and frees ENQ

**CWD handler:**
- Validates: no wildcards (`501`), valid qualifier chars
- Unquoted = relative (appends to prefix), quoted = absolute (resets prefix)
- Without trailing dot → `__dscbdv()` checks DSORG=PO → sets `sess->in_pds`
- With trailing dot → prefix only, no I/O
- When `in_pds`: RETR/STOR/DELE/SIZE treat args as member names: `DSN(MEMBER)`

**LIST formatting:**
- Dataset LIST: z/OS-compatible columnar format with Dsname **relative** to prefix
- PDS LIST (RECFM=F/V): ISPF stats via `__fmtisp()` — `YYYY/MM/DD` dates, `HH:MM` time
- PDS LIST (RECFM=U): Load module stats via `__fmtloa()` — SIZE, TTR, ATTR, SSI
- LIST output always translated to ASCII regardless of TYPE setting
- Empty result → `550 No data sets found.` (no data connection opened)

### Acceptance Criteria — ALL PASSED (23/23 automated tests)
- [x] CWD sets prefix; PWD returns z/OS format
- [x] CWD without trailing dot detects PDS via OBTAIN (DSORG=PO check)
- [x] CWD with trailing dot sets prefix only (no I/O)
- [x] CWD rejects wildcards with `501` (z/OS-compatible error messages)
- [x] Unquoted CWD is relative (accumulates), quoted CWD is absolute (resets)
- [x] LIST returns z/OS-compatible dataset listing with Dsname relative to prefix
- [x] LIST wildcards work: `*.LOAD`, `**.DATA`, `*`
- [x] LIST on PDS shows ISPF stats (RECFM=F/V) or load module stats (RECFM=U)
- [x] Empty LIST returns `550` (no header, no data connection)
- [x] RETR downloads dataset/member in TYPE A (ASCII) and TYPE I (binary)
- [x] STOR uploads with auto-allocation of new datasets via `__dsalcf()`
- [x] STOR binary roundtrip: MD5 verified (710400 bytes, 8880 records, FB 80)
- [x] STOR text roundtrip: special characters `[]{}|$@#` survive CP037 translation
- [x] PDS member upload/download in PDS context (`sess->in_pds`)
- [x] DELE deletes datasets and PDS members
- [x] MKD allocates new PDS; RMD scratches PDS
- [x] RNFR/RNTO renames datasets; RNFR state cleared on non-RNTO command
- [x] SIZE returns dataset size
- [x] LIST output always ASCII regardless of TYPE setting (FileZilla compat)
- [x] `fclose()` properly releases dataset (no ENQ leak)

---

## Step 1.5 — SITE Command Framework ✅ DONE

### What
SITE command dispatcher and dataset allocation parameter handling. Unknown z/OS SITE subcommands return `200` with warning (not `502`), matching z/OS behavior.

### Files to Create

| File | Purpose |
|------|---------|
| `src/ftpdsite.c` | SITE command parser and handlers |

### Implementation Details

**ftpdsite.c — SITE Commands:**

*Parser:*
- `ftpd_site_dispatch(sess, args)` — Parse SITE subcommand, dispatch to handler
- Case-insensitive subcommand matching
- Unrecognized subcommands → `200-Unrecognized parameter 'XXX' on SITE command.` / `200 SITE command was accepted` (matches z/OS lenient behavior)

*Bare SITE (no arguments):*
- Return `202 SITE not necessary; you may proceed` (matches z/OS). Use STAT for settings display.

*Implemented subcommands (set session state):*
- `SITE FILETYPE=SEQ` → `sess->filetype = FT_SEQ` (default)
- `SITE FILETYPE=JES` → `sess->filetype = FT_JES` (Phase 2 — for now return `200` but JES commands are not yet available)
- `SITE RECFM=xx` → `sess->alloc.recfm = xx`
- `SITE LRECL=nnn` → `sess->alloc.lrecl = nnn`
- `SITE BLKSIZE=nnn` → `sess->alloc.blksize = nnn` (validate: for FB, must be multiple of LRECL — auto-adjust downward if not, warn in response)
- `SITE PRIMARY=nnn` → `sess->alloc.primary = nnn`
- `SITE SECONDARY=nnn` → `sess->alloc.secondary = nnn`
- `SITE TRACKS` → `sess->alloc.spacetype = "TRK"`
- `SITE CYLINDERS` → `sess->alloc.spacetype = "CYL"`
- `SITE VOLUME=xxxxxx` → `sess->alloc.volume = xxxxxx`
- `SITE UNIT=xxxx` → `sess->alloc.unit = xxxx`
- `SITE DIRECTORY=nn` → `sess->alloc.dirblks = nn`
- `SITE TRAILING` → **Toggle** trailing blanks flag (default: OFF = blanks NOT removed, matching z/OS)
- `SITE TRUNCATE` → **Toggle** truncate flag (default: OFF)
- `SITE RDW` → **Toggle** RDW flag (default: OFF)
- `SITE SBSENDEOL=CRLF|LF` → Set EOL mode
- All respond `200 SITE command was accepted`
- All parameters are **sticky** — persist for entire session

*JES parameters (accepted, stored for Phase 2):*
- `SITE JESINTERFACELEVEL=n` → `sess->jes_level = n`
- `SITE JESJOBNAME=name` → `sess->jes_jobname = name`
- `SITE JESOWNER=owner` → `sess->jes_owner = owner`
- `SITE JESSTATUS=status` → `sess->jes_status = status`

*Accepted silently (return `200`, no effect — for client compatibility):*
- DATACLAS, STORCLAS, MGMTCLAS (SMS classes — MVS 3.8j has no SMS)

*All other unknown parameters:*
- `200-Unrecognized parameter 'XXX' on SITE command.` / `200 SITE command was accepted`

### Dependencies
- Step 1.3 (command dispatcher)
- Step 1.4 (allocation parameters used by STOR)

### Acceptance Criteria
- [ ] `SITE RECFM=VB` sets the session allocation RECFM
- [ ] `SITE LRECL=255` + `SITE BLKSIZE=6233` + `SITE RECFM=VB` → subsequent `STOR` creates dataset with these attributes
- [ ] `SITE BLKSIZE=6000` with `SITE RECFM=FB` + `SITE LRECL=80` → auto-adjusts to 5920 with warning
- [ ] `SITE FILETYPE=JES` returns `200` (JES mode set, but no JES commands available yet)
- [ ] `SITE FILETYPE=SEQ` switches back to sequential mode
- [ ] `SITE DATACLAS=xxx` returns `200` silently (no error)
- [ ] `SITE FOOBAR=123` returns `200` with `Unrecognized parameter` warning
- [ ] `SITE` with no arguments returns `202 SITE not necessary; you may proceed`
- [ ] `SITE TRAILING` toggles: first call enables, second call disables
- [ ] SITE parameters persist across multiple STOR operations within session
- [ ] `STAT` displays current SITE settings
- [ ] Zowe CLI `zowe zftp` can set SITE parameters without errors (manual test)

---

## Phase 1 Completion — ✅ DONE (2026-03-27)

**All steps completed. 23/23 automated tests pass.**

Verified via `test_ftpd.sh` comprehensive test suite:

1. ✅ **Binary Roundtrip (FB 80):** 710KB random data, STOR + RETR, MD5 match
2. ✅ **Text (TYPE A) Roundtrip:** Special characters `[]{}|$@#` survive CP037 translation
3. ✅ **PDS Operations:** MKD creates PDS, binary + text member upload/download roundtrip
4. ✅ **Cleanup:** DELE binary DS, DELE text DS, RMD PDS

**Known issues tracked separately:**
- TSK-87: AUTH TLS/SSL → 502 statt 530 (To Do, XS)
- TSK-88: crent370 ropen/rwrite RECFM bug (Backlog, M)
- TSK-89: APPE echtes Append statt Replace (Backlog, S)
- TSK-90: PDS Member Delete IDCAMS vs STOW (Backlog, S)
- TSK-91: CWD multi-level navigation (Backlog, S)

---

## Notes

- **Incremental development:** Each step can be built and tested independently. Steps 1.1 → 1.2 → 1.3 must be done in order. Steps 1.4 and 1.5 can overlap.
- **Error handling:** Every dataset operation can fail. Always return appropriate FTP reply codes (450, 451, 452, 550, 552, 553). Never crash on bad input.
- **Memory management:** Sessions must clean up all resources on disconnect. No memory leaks across sessions.
- **Thread safety:** Session state is per-thread. Global state (config, active session list) needs protection if accessed from multiple threads.
