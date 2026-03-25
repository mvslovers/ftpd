# FTPD Implementation Plan — Phase 5: SITE XMIT Support

**Phase:** 5 of 5  
**Goal:** Download/upload entire datasets (including PDS with all members) in TSO TRANSMIT format.  
**Prerequisite:** Phases 1-4 complete  
**Deliverable:** `SITE XMIT` mode for full dataset preservation transfers.

---

## Background

TSO TRANSMIT (XMIT) format packs a dataset — including all attributes (RECFM, LRECL, BLKSIZE, DSORG) and for PDS all members with directory — into a single sequential file. This is the standard MVS packaging format used by:
- MVP package manager (`.XMI` files)
- Software distribution (CBT tape, mvslovers GitHub Releases)
- Cross-system dataset migration
- NJE38 TRANSMIT/RECEIVE

`SITE XMIT` switches RETR to produce XMIT-format output and STOR to accept XMIT-format input.

---

## Dependency Strategy

The XMIT format requires a TRANSMIT/RECEIVE library. Two approaches:

**Option A — NJE38 dependency (initial):**
- Use NJE38's TRANSMIT/RECEIVE programs on MVS
- Soft dependency (like UFSD): if NJE38 not installed, SITE XMIT returns `502`
- Invoke TRANSMIT/RECEIVE via crent370 program call or temp dataset exchange

**Option B — Standalone C library (future):**
- A separate `mvslovers/xmitlib` project implementing XMIT format in C
- Link directly into FTPD, no external dependency
- Would also be usable by mbt, MVP, and other tools

Phase 5 starts with **Option A**. When the standalone library becomes available, it replaces the NJE38 dependency — the FTPD interface stays the same.

---

## Step 5.1 — SITE XMIT Mode

### What
Implement the SITE XMIT switch and the infrastructure for XMIT-mode transfers.

### Implementation Details

**SITE command additions in ftpdsite.c:**
- `SITE XMIT` → Enable XMIT mode: `sess->xmit_mode = 1`, reply `200 XMIT mode enabled`
- `SITE NOXMIT` → Disable XMIT mode: `sess->xmit_mode = 0`, reply `200 XMIT mode disabled`
- XMIT mode is orthogonal to FILETYPE — it affects RETR/STOR behavior in SEQ mode only
- In JES mode, XMIT has no effect

**Session state addition:**
```c
/* Add to ftpd_session_t */
int xmit_mode;              /* XMIT transfer mode active     */
```

**Availability check:**
- On first `SITE XMIT`: Check if NJE38 TRANSMIT/RECEIVE are available
- If not available → `502 XMIT not available (NJE38 not installed)`

### Acceptance Criteria
- [ ] `SITE XMIT` returns `200` when NJE38 is available
- [ ] `SITE XMIT` returns `502` when NJE38 is not installed
- [ ] `SITE NOXMIT` disables XMIT mode
- [ ] XMIT mode state is tracked per session

---

## Step 5.2 — XMIT Download (RETR)

### What
When XMIT mode is active, `RETR` produces the dataset in XMIT format instead of raw content.

### Implementation Details

**ftpdmvs.c — XMIT RETR flow:**
1. Client sends `RETR 'USER1.SOURCE'` (with XMIT mode active)
2. Server calls TRANSMIT to convert the dataset to XMIT format:
   - **Option A (NJE38):** Invoke TRANSMIT program, output to temp sequential dataset, then stream temp dataset to client
   - **Option B (future library):** Call `xmit_pack(dsname, output_stream)` directly
3. Transfer XMIT data on data connection (always binary — TYPE I recommended)
4. Reply: `226 Transfer complete (XMIT format)`

**Key benefit for PDS:**
- Without XMIT: `RETR 'USER1.SOURCE'` on a PDS → error (can't download a PDS as a file)
- With XMIT: `RETR 'USER1.SOURCE'` on a PDS → complete PDS as single XMIT file, all members included

**Temp dataset handling:**
- Allocate temp dataset for TRANSMIT output: `&&XMIT` with sufficient space
- After transfer: delete temp dataset
- Naming: `{HLQ}.FTPD.XMIT.T{timestamp}` or SVC 99 temporary allocation

### Acceptance Criteria
- [ ] `SITE XMIT` + `RETR 'USER1.SEQ.DATA'` downloads a sequential dataset in XMIT format
- [ ] `SITE XMIT` + `RETR 'USER1.PDS.LIB'` downloads an entire PDS (all members) in XMIT format
- [ ] Downloaded XMIT file can be received on another MVS system via RECEIVE/RECV370
- [ ] After RECEIVE, the restored dataset has identical attributes (RECFM, LRECL, BLKSIZE, DSORG)
- [ ] After RECEIVE of a PDS, all members are present with correct content
- [ ] Temp datasets are cleaned up after transfer
- [ ] Transfer in TYPE A still works but TYPE I is recommended for XMIT (warn if TYPE A?)

---

## Step 5.3 — XMIT Upload (STOR)

### What
When XMIT mode is active, `STOR` accepts XMIT-format input and restores the dataset.

### Implementation Details

**ftpdmvs.c — XMIT STOR flow:**
1. Client sends `STOR 'USER1.RESTORED.DATA'` (with XMIT mode active)
2. Server receives XMIT data from data connection
3. Write to temp sequential dataset
4. Call RECEIVE to unpack:
   - **Option A (NJE38):** Invoke RECEIVE program with temp dataset as input, target dsname as output
   - **Option B (future library):** Call `xmit_unpack(input_stream, dsname)` directly
5. Delete temp dataset
6. Reply: `250 Dataset restored from XMIT format`

**Dataset naming:**
- The target dataset name comes from the STOR argument
- If the dataset already exists → error `550 Dataset already exists` (or overwrite if user confirms via SITE option?)
- Dataset attributes are taken from the XMIT file, not from SITE RECFM/LRECL etc.

**Volume selection:**
- Use `sess->alloc.volume` if set via SITE VOLUME, otherwise system default

### Acceptance Criteria
- [ ] `SITE XMIT` + `STOR 'USER1.RESTORED'` accepts an XMIT file and restores the dataset
- [ ] Restored sequential dataset has correct attributes from the XMIT file
- [ ] Restored PDS has all members with correct content
- [ ] If target dataset already exists → `550`
- [ ] Temp datasets are cleaned up after restore
- [ ] Invalid XMIT file → `451 Invalid XMIT format`

---

## Phase 5 Completion Criteria

The overall test for XMIT support:

1. **Download PDS as XMIT:**
   - `SITE XMIT` → `200`
   - `TYPE I` → `200`
   - `GET 'SYS1.PARMLIB' parmlib.xmi` → `226 Transfer complete (XMIT format)`
   - Verify: `parmlib.xmi` is a valid XMIT file

2. **Upload XMIT to restore PDS:**
   - `SITE XMIT` → `200`
   - `TYPE I` → `200`
   - `PUT parmlib.xmi 'IBMUSER.PARMLIB.COPY'` → `250 Dataset restored from XMIT format`
   - Verify: `IBMUSER.PARMLIB.COPY` is a PDS with same members and attributes as `SYS1.PARMLIB`

3. **Round-trip test:**
   - Download any dataset in XMIT mode
   - Upload the XMIT file as a new dataset
   - Compare original and restored dataset: attributes and content must be identical

4. **Compatibility:**
   - XMIT files produced by FTPD can be received by RECV370 and NJE38 RECEIVE
   - XMIT files produced by NJE38 TRANSMIT can be uploaded via FTPD STOR in XMIT mode

---

## Future: Standalone XMIT Library

When `mvslovers/xmitlib` becomes available:

1. Add `"mvslovers/xmitlib" = ">=1.0.0"` to `project.toml` dependencies
2. Replace NJE38 invocations with direct library calls
3. Remove temp dataset overhead (stream directly)
4. Remove NJE38 soft dependency check
5. Significantly faster transfers (no program invocation overhead)

The FTPD interface (`SITE XMIT`, `SITE NOXMIT`) stays unchanged.
