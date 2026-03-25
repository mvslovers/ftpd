# FTPD Implementation Plan — Phase 3: UFS Support via UFSD

**Phase:** 3 of 5  
**Goal:** Seamless UFS file access alongside MVS datasets, with automatic mode switching.  
**Prerequisite:** Phase 1 complete (Phase 2 is independent — Phases 2 and 3 can be developed in parallel)  
**Deliverable:** Full dual-filesystem FTP server (MVS + UFS).

---

## Environment Notes for Claude Code

- UFSD client library: check `../ufsd/client/` for the client API and `../ufsd/include/` for shared headers.
- UFSD communicates via cross-memory services (crent370 `ipc/` module). The client library abstracts this — FTPD calls the client API, not IPC directly.
- UFSD is a **soft dependency**. FTPD must start and work without UFSD. UFS commands return `550 UFS service not available` if UFSD is not running.
- Reference: check `../httpd/src/` for how HTTPD uses UFS (though HTTPD uses the old UFS370 library, the concepts are similar).

---

## Step 3.1 — UFSD Client Integration

### What
Integrate UFSD client library, detect availability, and provide a clean wrapper layer.

### Files to Create

| File | Purpose |
|------|---------|
| `src/ftpd#ufs.c` | UFS operations wrapper around UFSD client library |

### Implementation Details

**ftpd#ufs.c — UFSD Client Wrapper:**

*Availability detection:*
- `ftpd_ufs_available()` — Check if UFSD subsystem is running (try connecting via client library). Cache result per session with periodic re-check.
- Called on first UFS command in a session, not at server startup.

*Wrapper functions (delegate to UFSD client API):*
- `ftpd_ufs_stat(path, info)` — Get file/directory attributes
- `ftpd_ufs_open(path, mode)` — Open file for read or write
- `ftpd_ufs_read(fd, buf, len)` — Read from file
- `ftpd_ufs_write(fd, buf, len)` — Write to file
- `ftpd_ufs_close(fd)` — Close file
- `ftpd_ufs_opendir(path)` — Open directory for listing
- `ftpd_ufs_readdir(dirfd)` — Read next directory entry
- `ftpd_ufs_closedir(dirfd)` — Close directory
- `ftpd_ufs_mkdir(path)` — Create directory
- `ftpd_ufs_rmdir(path)` — Remove directory
- `ftpd_ufs_unlink(path)` — Delete file
- `ftpd_ufs_rename(old, new)` — Rename file or directory

*Error mapping:*
- Map UFSD error codes to FTP reply codes:
  - File not found → `550`
  - Permission denied → `550`
  - Directory not empty → `550`
  - Disk full → `452`
  - UFSD not available → `550 UFS service not available`

*Path handling:*
- `ftpd_ufs_resolve(sess, input, output)` — Resolve relative paths against `sess->ufs_cwd`
- Handle `.`, `..`, and absolute paths
- Prevent path traversal above UFS root

### Dependencies
- Phase 1 (session state, data connections)
- mvslovers/ufsd client library (resolved via mbt dependency)
- crent370: `ipc/` module (used internally by UFSD client)

### Acceptance Criteria
- [ ] `ftpd_ufs_available()` returns true when UFSD is running, false when not
- [ ] All wrapper functions correctly delegate to UFSD client API
- [ ] Errors from UFSD are mapped to appropriate FTP reply codes
- [ ] When UFSD is not running, all UFS operations return `550 UFS service not available`
- [ ] Relative path resolution works: `./foo`, `../bar`, `subdir/file`
- [ ] Path traversal above root is blocked

---

## Step 3.2 — UFS Operations

### What
Implement FTP commands for UFS file operations: LIST, RETR, STOR, DELE, MKD, RMD, RNFR/RNTO, SIZE, MDTM.

### Implementation Details

**FTP command handlers in ftpd#ufs.c:**

*Directory navigation:*
- `CWD /path` → Set `sess->ufs_cwd`, switch `sess->fsmode = FS_UFS`
- `CWD ..` or `CDUP` in UFS mode → Parent directory
- `PWD` in UFS mode → `257 "/current/path" is working directory`

*Directory listing:*
- `LIST` in UFS mode → Unix-style long listing via data connection:
  ```
  drwxr-xr-x  2 user  group      4096 Mar 12 14:30 subdir
  -rw-r--r--  1 user  group      1234 Mar 11 09:15 hello.c
  ```
- `NLST` → Name-only listing, one filename per line
- Format each entry: permissions, link count, owner, group, size, date, name
- Date format: `Mon DD HH:MM` for recent files, `Mon DD  YYYY` for older files

*File transfer:*
- `RETR path` → Open file via UFSD, read and send on data connection
  - TYPE A: translate EBCDIC → ASCII (UFS stores EBCDIC)
  - TYPE I: no translation
- `STOR path` → Receive from data connection, write file via UFSD
  - TYPE A: translate ASCII → EBCDIC
  - TYPE I: no translation
  - Create parent directories? → No, match Unix behavior (parent must exist)
- `APPE path` → Append to existing file

*File management:*
- `DELE path` → `ftpd_ufs_unlink()`
- `MKD path` → `ftpd_ufs_mkdir()`, reply `257 "path" directory created`
- `RMD path` → `ftpd_ufs_rmdir()` (must be empty)
- `RNFR path` → Store source in `sess->rnfr_path`
- `RNTO path` → `ftpd_ufs_rename(rnfr_path, new_path)`

*File metadata:*
- `SIZE path` → `ftpd_ufs_stat()`, reply `213 size_in_bytes`
- `MDTM path` → `ftpd_ufs_stat()`, reply `213 YYYYMMDDHHMMSS`

**Command routing in ftpd#cmd.c:**
- Check `sess->fsmode`: if `FS_UFS`, route to `ftpd#ufs.c` handlers
- If `FS_MVS`, route to `ftpd#mvs.c` handlers (existing Phase 1 code)
- `SITE FILETYPE=JES` overrides both (Phase 2)

### Dependencies
- Step 3.1 (UFSD wrapper)
- Phase 1 (data connections, command dispatch, translation)

### Acceptance Criteria
- [ ] `CWD /` switches to UFS mode
- [ ] `PWD` in UFS mode returns `257 "/" is working directory`
- [ ] `LIST` in UFS mode returns Unix-style long listing
- [ ] `NLST` returns filename-only listing
- [ ] `RETR /path/to/file` downloads a UFS file (ASCII mode: correct translation)
- [ ] `RETR` in binary mode (TYPE I) transfers bytes unchanged
- [ ] `STOR /path/to/file` uploads a file to UFS
- [ ] `DELE /path/to/file` deletes a UFS file
- [ ] `MKD /path/to/newdir` creates a directory
- [ ] `RMD /path/to/emptydir` removes an empty directory
- [ ] `RMD` on non-empty directory → `550`
- [ ] `RNFR` + `RNTO` renames a file
- [ ] `SIZE` returns correct file size
- [ ] `MDTM` returns correct modification timestamp
- [ ] When UFSD is not running: `CWD /` → `550 UFS service not available`

---

## Step 3.3 — Hybrid Navigation

### What
Seamless switching between MVS and UFS mode within a single session.

### Implementation Details

**Mode switching rules:**
- `CWD /anything` → Switch to UFS mode (`sess->fsmode = FS_UFS`)
- `CWD 'HLQ.DSN'` (quoted) → Switch to MVS mode (`sess->fsmode = FS_MVS`)
- `CWD unqualified` in MVS mode → Update MVS prefix
- `CWD relative` in UFS mode → Navigate within UFS
- `CDUP` in UFS mode → Parent directory (stay in UFS)
- `CDUP` in MVS mode → Remove last qualifier from prefix

**PWD reflects current mode:**
- MVS: `257 "'IBMUSER.'" is working directory`
- UFS: `257 "/home/ibmuser" is working directory`

**LIST behavior:**
- In MVS mode → MVS dataset listing format
- In UFS mode → Unix-style listing format
- Client (FileZilla etc.) auto-detects format from LIST output

**Edge cases:**
- `CWD /` when UFSD not running → `550 UFS service not available` (stay in MVS mode)
- `SITE FILETYPE=JES` while in UFS mode → Switch to JES mode (fsmode irrelevant in JES)
- `SITE FILETYPE=SEQ` while in JES mode → Return to previous fsmode (MVS or UFS)

**Initial session state:**
- After login: `sess->fsmode = FS_MVS`, `sess->mvs_cwd = userid + "."`
- UFS mode only entered explicitly via `CWD /...`

### Dependencies
- Steps 3.1 + 3.2
- Phase 1 (MVS dataset operations)

### Acceptance Criteria
- [ ] After login, default mode is MVS: `PWD` → `257 "'IBMUSER.'"`
- [ ] `CWD /` switches to UFS: `PWD` → `257 "/" is working directory`
- [ ] `CWD 'SYS1.'` switches back to MVS: `PWD` → `257 "'SYS1.'"`
- [ ] `LIST` output format changes depending on mode (MVS format vs Unix format)
- [ ] Navigation within UFS mode works: `CWD subdir`, `CDUP`
- [ ] Navigation within MVS mode works: `CWD LOAD`, `CDUP`
- [ ] Switching modes multiple times in one session works without errors
- [ ] `SITE FILETYPE=JES` → JES mode → `SITE FILETYPE=SEQ` → returns to previous FS mode
- [ ] FileZilla can navigate between MVS and UFS in one session (manual test)
- [ ] If UFSD goes down mid-session, UFS commands fail gracefully with `550`

---

## Phase 3 Completion Criteria

The overall test for UFS + hybrid navigation:

1. Login → MVS mode by default
2. `LIST` → MVS dataset listing
3. `CWD /` → UFS mode
4. `LIST` → Unix-style directory listing
5. `MKD /tmp/ftptest` → directory created
6. `CWD /tmp/ftptest` → navigate into it
7. `PUT local.txt hello.txt` → upload file to UFS
8. `LIST` → shows `hello.txt`
9. `GET hello.txt local2.txt` → download, files match
10. `DELE hello.txt` → file deleted
11. `CWD 'IBMUSER.'` → back to MVS mode
12. `LIST` → MVS dataset listing again
13. `RMD /tmp/ftptest` → directory removed
