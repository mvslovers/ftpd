include ../CLAUDE.md

override C standard and use gnu99

## Project: FTPD — Standalone FTP Server for MVS 3.8j

Concept & architecture: `doc/FTPD_CONCEPT.md`
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
- **Config:** Key=value file in `SYS1.PARMLIB(FTPDPM00)`
- **Console:** `/S FTPD`, `/P FTPD`, `/F FTPD,D SESSIONS|STATS|VERSION|CONFIG`, `/F FTPD,TRACE ON|OFF|DUMP`

### Source Module Map

| File | Role |
|------|------|
| `ftpd.c` | Main: listener, console commands, shutdown |
| `ftpdses.c` | Session state machine + thread lifecycle |
| `ftpdcmd.c` | Command parser & dispatcher |
| `ftpdmvs.c` | MVS dataset ops (VTOC, OBTAIN, dynalloc, OPEN/CLOSE) |
| `ftpdufs.c` | UFS ops via UFSD client library |
| `ftpdjes.c` | JES interface (submit, list, retrieve spool) |
| `ftpddata.c` | Data connection management (PORT/PASV) |
| `ftpdxlat.c` | EBCDIC ↔ ASCII translation tables |
| `ftpdauth.c` | Authentication (RAKF via crent370 racf) |
| `ftpdsite.c` | SITE command processing |
| `ftpdlist.c` | LIST/NLST formatting (MVS + UFS + JES) |
| `ftpdlog.c` | Logging (WTO + STDOUT) + trace ring buffer |
| `ftpdcfg.c` | Configuration file parsing |

### Implementation Phases

1. **Foundation** — Core FTP + MVS datasets (scaffolding → network → commands → dataset access → SITE)
2. **JES Interface** — Job submission, status, spool retrieval
3. **UFS Support** — UFSD client integration, hybrid MVS/UFS navigation
4. **Polish** — Console commands, timeouts, error handling, packaging
5. **SITE XMIT** — TRANSMIT-format dataset transfer
