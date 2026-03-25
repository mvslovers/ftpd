# FTPD Implementation Plan — Phase 4: Polish & Hardening

**Phase:** 4 of 5  
**Goal:** Production quality — connection management, error handling, performance, documentation, packaging.  
**Prerequisite:** Phases 1-3 complete (core FTP + JES + UFS all working)  
**Deliverable:** A robust, well-documented FTP server ready for general use.

---

## Step 4.1 — Connection Management

### What
Enforce connection limits, implement idle timeout, and improve session lifecycle management.

### Implementation Details

**Connection limits:**
- `ftpd_session_count()` — Track number of active sessions
- Reject new connections when `max_sessions` reached → `421 Maximum sessions reached, try again later`
- Optional: per-user session limit (e.g. max 3 sessions per userid)

**Idle timeout:**
- Each session tracks `last_activity` timestamp (updated on every command)
- Timeout checker: either in the session's read loop (poll/select with timeout) or via a periodic check in the main thread
- On timeout: send `421 Connection timed out` → close session
- Configurable via `IDLETIMEOUT=nnn` in config (default 300 seconds)

**Graceful shutdown improvements:**
- `/P FTPD` → Set shutdown flag
- Stop accepting new connections
- Send `421 Server shutting down` to all active sessions
- Wait for active transfers to complete (with a hard timeout of 30 seconds)
- Clean exit

**Session tracking:**
- Assign session IDs (sequential counter)
- `/F FTPD,D SESSIONS` displays: session ID, userid, client IP, idle time, current state
- `/F FTPD,KILL sessid` force-closes a specific session

### Acceptance Criteria
- [ ] Connections beyond `max_sessions` are rejected with `421`
- [ ] Idle connections are automatically closed after configured timeout
- [ ] `/F FTPD,D SESSIONS` shows all active sessions with details
- [ ] `/F FTPD,KILL n` disconnects the specified session
- [ ] `/P FTPD` cleanly shuts down even with active sessions
- [ ] No resource leaks after sessions are closed (sockets, memory, allocated datasets)

---

## Step 4.2 — Transfer Restart (REST)

### What
Implement the REST command for resuming interrupted transfers (RFC 3659).

### Implementation Details

**REST command:**
- `REST nnn` → Store restart offset in `sess->rest_offset`, reply `350 Restart position accepted`
- On next `RETR`: seek to offset before sending data
- On next `STOR`: seek to offset before writing data (append from position)
- REST offset is cleared after the next transfer command (whether it succeeds or fails)
- Only supported for `MODE S` (Stream) — `REST STREAM` is advertised in FEAT

**Constraints:**
- For MVS sequential datasets: REST offset is a byte offset. For FB datasets, must align to record boundary (offset % lrecl == 0)
- For PDS members: REST works like sequential
- For UFS files: standard byte offset, no alignment requirement
- For JES mode: REST not supported → `502`

### Acceptance Criteria
- [ ] `REST 1000` + `RETR file` starts transfer from byte 1000
- [ ] `REST 0` resets to beginning
- [ ] REST offset is cleared after transfer
- [ ] REST in JES mode → `502`
- [ ] Interrupted download can be resumed with REST (test with curl `--continue-at`)
- [ ] `FEAT` response includes `REST STREAM`

---

## Step 4.3 — Error Handling & Reply Codes

### What
Comprehensive error handling with correct FTP reply codes for all failure conditions.

### Implementation Details

**Review and fix all error paths:**
- Every dataset operation must handle failures (allocation failure, I/O error, permission denied, dataset busy)
- Every socket operation must handle errors (connection reset, broken pipe)
- Map all MVS abend codes and return codes to appropriate FTP replies
- Map all UFSD errors to appropriate FTP replies

**Specific error scenarios:**
| Scenario | Reply |
|----------|-------|
| Dataset not found | `550 dsname not found` |
| Dataset in use (DISP conflict) | `450 Dataset busy, try again later` |
| Permission denied (RACF) | `550 Access denied` |
| Insufficient space for STOR | `452 Insufficient storage space` |
| Invalid dataset name | `553 Dataset name not allowed` |
| ABEND during I/O | `451 Local error in processing` |
| Data connection failure | `426 Connection closed, transfer aborted` |
| Cannot open data connection | `425 Can't open data connection` |
| PDS directory full | `452 Insufficient storage space` |
| Volume not mounted | `550 Volume not available` |

**Multi-line replies:**
- Some responses need multi-line format (e.g. `250-It is known to JES as JOB00042` / `250 Transfer completed successfully`)
- Ensure all multi-line replies follow FTP convention: `NNN-text` for continuation, `NNN text` for last line

**Robustness:**
- Never crash on malformed client input (empty commands, overlong lines, binary garbage)
- Maximum command line length: 512 bytes (truncate or reject longer lines)
- Handle clients that send commands too fast (pipeline commands before reply)

### Acceptance Criteria
- [ ] Every dataset operation failure returns an appropriate FTP reply code
- [ ] Malformed commands do not crash the server
- [ ] Overlong command lines are handled gracefully (500 or truncation)
- [ ] Binary garbage on control connection does not crash the session
- [ ] Data connection failures produce correct 425/426 replies
- [ ] Multi-line replies are correctly formatted
- [ ] All 550 replies include a descriptive message (not just the code)

---

## Step 4.4 — Performance

### What
Optimize VTOC scanning, data transfer buffering, and general throughput.

### Implementation Details

**VTOC scan optimization:**
- Read VTOC blocks in larger chunks where possible (multi-track reads)
- Skip Format-0 (empty) DSCBs quickly
- Pattern matching: reject non-matching DSCBs at the first qualifier level before full comparison

**Transfer buffering:**
- Increase socket send/receive buffer sizes (SO_SNDBUF/SO_RCVBUF) for data connections
- Buffer dataset reads: read multiple blocks at once, then write to socket in one call
- For FB datasets: read full blocks (BLKSIZE), deblock into records, trim/translate, send
- For VB datasets: handle variable-length records correctly with BDW/RDW

**Connection reuse:**
- Don't close/reopen PASV listener between transfers if client supports it
- Reuse allocated dataset handles within a session where safe

### Acceptance Criteria
- [ ] `LIST` on a volume with 200+ datasets completes in reasonable time (< 5 seconds)
- [ ] Large file transfers (>1MB) sustain reasonable throughput
- [ ] FB dataset deblocking is correct for various BLKSIZE values
- [ ] VB dataset RDW/BDW handling is correct
- [ ] Memory usage stays bounded regardless of transfer size (streaming, not buffering entire file)

---

## Step 4.5 — Documentation

### What
README, admin guide, and inline code documentation.

### Files to Create

| File | Purpose |
|------|---------|
| `README.md` | Project overview, features, quick start |
| `doc/ADMIN.md` | Administration guide: installation, configuration, console commands |
| `doc/COMPAT.md` | Client compatibility notes (FileZilla, Zowe, etc.) |

### Content

**README.md:**
- Project description and features
- Quick start (clone, build, configure, start)
- Supported FTP commands
- Supported SITE subcommands
- Dependencies (crent370, ufsd)
- License

**ADMIN.md:**
- Installation steps (from XMI or via mbt build)
- RAKF configuration (FTPD user, FTPAUTH facility class)
- Configuration file reference (all FTPDPM00 parameters)
- JCL procedure
- Console commands reference
- Logging and troubleshooting
- DASD volume configuration

**COMPAT.md:**
- FileZilla: server type settings, known quirks
- Zowe CLI: zftp plugin configuration
- zos-node-accessor: connection options
- Windows/Linux ftp command line
- curl --ftp usage
- Known differences from z/OS FTP behavior

### Acceptance Criteria
- [ ] README covers all features and has a working quick-start guide
- [ ] ADMIN guide covers installation, configuration, and all console commands
- [ ] COMPAT guide has tested instructions for at least 3 clients
- [ ] All public functions in source code have header comments explaining purpose and parameters

---

## Step 4.6 — Packaging & Distribution

### What
MVP package for MVS/CE, mbt release artifacts, CI/CD pipeline.

### Implementation Details

**MVP package:**
- Create `FTPD.MVP` package descriptor (like existing FTPD)
- Installation via `RX MVP INSTALL FTPD`
- Includes: load module, STC procedure, PARMLIB member, RAKF updates

**mbt packaging:**
- `make package` produces release artifacts in `dist/`:
  - `ftpd-{version}-mvs.tar.gz` — XMIT files for load module
  - `package.toml` — manifest
- `make release VERSION=x.y.z` — tag, build, publish to GitHub Releases

**CI/CD:**
- `.github/workflows/build.yml` — Build on push/PR (uses mbt reusable workflow)
- `.github/workflows/release.yml` — Release on tag push

### Acceptance Criteria
- [ ] `make package` creates valid release artifacts
- [ ] `make release VERSION=1.0.0` creates a GitHub Release with assets
- [ ] `RX MVP INSTALL FTPD` installs the server on a fresh MVS/CE system
- [ ] After MVP install: `/S FTPD` starts the server, FTP clients can connect
- [ ] CI pipeline runs on every push and reports build status

---

## Phase 4 Completion Criteria

The server is production-ready when:

1. Runs stable for extended periods (hours) without memory leaks or crashes
2. Handles concurrent sessions without corruption
3. Gracefully handles all error conditions (disk full, permission denied, network errors)
4. All console commands work correctly
5. Idle connections are automatically cleaned up
6. Documentation is complete and accurate
7. MVP package installs cleanly on a fresh MVS/CE
8. CI/CD pipeline is operational
