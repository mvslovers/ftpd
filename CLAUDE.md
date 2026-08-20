include ../CLAUDE.md

override C standard and use gnu99

## Project: FTPD — Standalone FTP Server for MVS 3.8j

Concept & architecture: `doc/FTPD_CONCEPT.md`
RAKF setup guide: `doc/FTPD_RAKF_SETUP.md`
Phase 1 implementation plan: `doc/FTPD_PHASE1_PLAN.md`

### What This Is

Standalone FTP daemon for MVS 3.8j. Not tied to HTTPD.
Supports native MVS datasets, UFS (via UFSD), and JES (job submit/spool).
z/OS-compatible SITE commands and dataset name handling.

### No writable module data — FTPD is AC(1)

**Never add a mutable file-scope variable or a function-local `static` to a
`[[module]]` source.** FTPD is link-edited `AC(1)`. Fetched from an
APF-authorized library, the job step is authorized before program fetch, so
MVS loads the module into subpool 252 **key 0**; the STC runs problem state
key 8, and any store into module storage abends S0C4 (#101). It also breaks
the RENT/REUS attributes ld370 puts on the module by default.

Where state goes instead: into `struct ftpd_server` (a `main()` local, key 8)
and published process-wide through the GRT — `grtapp1` anchors the server for
dump reading, `grtapp2` the log/trace block that `ftpd#log.c` reads on every
call. `ftpd_log_anchor()` is the pattern to copy. A subtask inherits the GRT
from its mother task (`@@CRTSET`), so worker threads see it. Tables that are
only ever read are `const` instead.

`tools/check-module-data.py` enforces it and runs as its own CI job. It is a
text proxy — the real check is `cc370 -S` plus a look for a store through a
register loaded from `=A(@Vn)` or into an X-var.

### Dependencies

- **crent370** (required): C runtime — sockets, thdmgr (threads), jes, racf, os, emfile, ipc
- **ufsd** (soft/optional): UFS filesystem access via cross-address-space client library.
  If UFSD not running, UFS commands return `550 UFS service not available`.

### Architecture Summary

- **Threading:** One thread per client session via crent370 `thdmgr`
- **Encoding:** EBCDIC internal, ASCII conversion at network I/O boundary (`ftpdxlat`)
- **Dataset catalog:** Abstract provider interface; initial impl = per-session filtered VTOC scan
- **Auth:** RAKF via crent370 `racf` module (FACILITY class FTPAUTH)
- **Config:** Key=value file via `DD:FTPDPRM` (JCL: `//FTPDPRM DD DSN=&D(&M),DISP=SHR,FREE=CLOSE`)
- **Console:** `/S FTPD`, `/P FTPD`, `/F FTPD,STATS|SESSIONS|CONFIG|VERSION|HELP|SHUTDOWN`, `/F FTPD,TRACE ON|OFF|DUMP`

### Source Module Map

Naming convention follows UFSD: `ftpd#xxx.c` / `ftpd#xxx.h` with 3-letter domain codes.

| File | Role |
|------|------|
| `ftpd.c` | Main: listener, event loop, shutdown |
| `ftpd#con.c` | Console command handler (CIB processing, MODIFY dispatch) |
| `ftpd#ses.c` | Session state machine + thread lifecycle |
| `ftpd#cmd.c` | FTP command parser & dispatcher |
| `ftpd#mvs.c` | MVS dataset ops (VTOC, OBTAIN, dynalloc, OPEN/CLOSE) |
| `ftpd#ufs.c` | UFS ops via UFSD client library |
| `ftpd#jes.c` | JES interface (submit, list, retrieve spool) |
| `ftpd#dat.c` | Data connection management (PORT/PASV) |
| `ftpd#xlt.c` | EBCDIC ↔ ASCII translation tables |
| `ftpd#aut.c` | Authentication (RAKF via crent370 racf) |
| `ftpd#sit.c` | SITE command processing |
| `ftpd#lst.c` | LIST/NLST formatting (MVS + UFS + JES) |
| `ftpd#log.c` | Logging (WTO + STDOUT) + trace ring buffer |
| `ftpd#cfg.c` | Configuration file parsing |

### Implementation Phases

1. **Foundation** — Core FTP + MVS datasets (scaffolding → network → commands → dataset access → SITE)
2. **JES Interface** — Job submission, status, spool retrieval
3. **UFS Support** — UFSD client integration, hybrid MVS/UFS navigation
4. **Polish** — Console commands, timeouts, error handling, packaging
5. **SITE XMIT** — TRANSMIT-format dataset transfer
