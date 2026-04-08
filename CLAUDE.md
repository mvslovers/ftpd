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
