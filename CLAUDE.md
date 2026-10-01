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

- **libc370 2.0** (required): the C runtime, from the cc370 sysroot (`[toolchain] libc370`) —
  sockets, threads, JES2, RACF, dynalloc. Not a `[dependencies]` entry.
- **ufsd** (soft/optional, `>=1.4.0-dev` — the first libc370 2.0 build): UFS filesystem access via cross-address-space client library.
  If UFSD not running, UFS commands return `550 UFS service not available`.

### Architecture Summary

- **Threading:** One thread per client session via the libc370 thread manager (`<mvs/thread.h>`)
- **Encoding:** EBCDIC internal, ASCII conversion at network I/O boundary (`ftpdxlat`)
- **Dataset catalog:** Abstract provider interface; initial impl = per-session filtered VTOC scan
- **Auth:** RAKF via libc370 `<mvs/racf.h>` (FACILITY class FTPAUTH)
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
| `ftpd#aut.c` | Authentication (RAKF via libc370 racf) |
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

## SMP4 FMID — one per release

The id is the release: `T` + three product letters + the three version digits.
One id per release, **spent exactly once**, and each release's SYSMOD deletes
its predecessor:

```toml
[distribution.smp]
fmid   = "TFTP120"
delete = ["TFTP110"]
```

**No version component may ever exceed 9** — a 7-character id has no room for
a second digit. At patch 9 cut the next minor, at minor 9 the next major;
ftpd 1.2.10 cannot be expressed and must not be released.

Current: **`TFTP120`** for 1.2.0, deleting `TFTP110`. `TFTP120` is not yet
checked on any stand. `TFTP111` was assigned to a 1.1.1 that was never cut (the
libc370 2.0 port made the next release 1.2.0) and is unspent and unassigned.
Burned: `TFTP100` (1.0.0–1.0.2) and `TFTP110` (1.1.0, released 2026-09-14).

Never re-spend an id, and never install a test package under the real one: a
test needs a throwaway id **and** throwaway module names, because SMP keys
element ownership on `MOD(name)`, not on the target library. See the root
`CLAUDE.md` for the full rule and the measurements behind it.
