# FTPD Implementation Plan — Phase 2: JES Interface

**Phase:** 2 of 5  
**Goal:** Submit jobs and retrieve spool output via FTP, compatible with Zowe CLI and zos-node-accessor.  
**Prerequisite:** Phase 1 complete (working FTP server with MVS dataset support)  
**Deliverable:** Full JES interface via `SITE FILETYPE=JES`.

---

## Environment Notes for Claude Code

- Same as Phase 1. Additionally: check `../crent370/jes/` for JES2 interaction APIs (internal reader, job status, spool access).
- The `SITE FILETYPE=JES` mode switch was already stubbed in Phase 1 Step 1.5. This phase implements the actual JES logic behind it.
- Reference: check how HTTPD's JES2 integration works at `../httpd/src/` (HTTPJES2 module).

---

## Step 2.1 — Job Submission

### What
When `SITE FILETYPE=JES` is active, `STOR` submits JCL to JES via the internal reader instead of writing to a dataset.

### Files to Create

| File | Purpose |
|------|---------|
| `src/ftpd#jes.c` | JES interface: submit, list, retrieve, delete |

### Implementation Details

**ftpd#jes.c — Job Submission:**
- `ftpd_jes_submit(sess)` — Called when STOR is issued in JES mode:
  1. Open internal reader programmatically via crent370's `jes/` module
  2. Read JCL records from data connection
  3. Translate ASCII → EBCDIC (TYPE A) or pass through (TYPE E)
  4. Write each 80-byte record to internal reader
  5. Close internal reader
  6. Parse JES2 response to extract job number (JOBnnnnn)
  7. Reply: `250-It is known to JES as JOBnnnnn` / `250 Transfer completed successfully.`
- Handle submission errors: malformed JCL, reader failure → `451 Requested action aborted`

**Command routing in ftpd#cmd.c:**
- When `sess->filetype == FT_JES`, route `STOR` to `ftpd_jes_submit()` instead of `ftpd_mvs_write_seq()`

### Dependencies
- Phase 1 (data connections, command dispatcher, SITE FILETYPE switch)
- crent370: `jes/` module (internal reader open/write/close)

### Acceptance Criteria
- [ ] `SITE FILETYPE=JES` → `200 SITE command was accepted`
- [ ] `STOR` in JES mode accepts JCL from data connection
- [ ] Server responds with `250-It is known to JES as JOBnnnnn` containing the actual job number
- [ ] Submitted job appears in JES2 queues (verify via console `$D J'jobname'`)
- [ ] Invalid JCL submission still returns job number (JES rejects the job, not FTPD)
- [ ] `SITE FILETYPE=SEQ` switches back to dataset mode, `STOR` works normally again

---

## Step 2.2 — Job Query

### What
`LIST` in JES mode returns jobs in JES queues, filterable by owner, name, and status.

### Implementation Details

**ftpd#jes.c — Job Listing:**
- `ftpd_jes_list(sess)` — Called when LIST is issued in JES mode:
  1. Query JES2 via crent370's `jes/` module
  2. Apply filters: `sess->jes_owner`, `sess->jes_jobname`, `sess->jes_status`
  3. Format output in z/OS-compatible format
  4. Send on data connection

**JES Interface Level behavior:**
- Level 1 (`sess->jes_level == 1`):
  - Only show jobs where jobname matches userid (or userid + 1 char)
  - Default: show only own jobs
- Level 2 (`sess->jes_level == 2`):
  - Any job name allowed
  - `SITE JESOWNER=*` required to see all own jobs
  - Default JESOWNER is the logged-in userid

**Job list format (z/OS compatible):**
```
JOBNAME  JOBID    OWNER    STATUS CLASS
IBMUSERJ JOB00042 IBMUSER  OUTPUT A     RC=0000
IBMUSERA JOB00043 IBMUSER  ACTIVE A
IBMUSERB JOB00044 IBMUSER  INPUT  A
```

**Spool file list (when LIST is called with a specific job ID):**
```
JOBNAME  JOBID    STEPNAME PROCSTEP C DDNAME   BYTE-COUNT
IBMUSERJ JOB00042 JES2              A JESMSGLG      1234
IBMUSERJ JOB00042 JES2              A JESJCL         567
IBMUSERJ JOB00042 STEP1             A SYSPRINT      8901
```

**Filter logic:**
- `SITE JESJOBNAME=IBMU*` → only jobs matching pattern
- `SITE JESOWNER=IBMUSER` → only jobs owned by IBMUSER
- `SITE JESOWNER=*` → all jobs owned by current user (Level 2)
- `SITE JESSTATUS=OUTPUT` → only completed jobs
- `SITE JESSTATUS=ALL` → jobs in any state

### Dependencies
- Step 2.1 (ftpd#jes.c base)
- crent370: `jes/` module (job status query)

### Acceptance Criteria
- [ ] `LIST` in JES mode returns job listing in z/OS-compatible format
- [ ] `SITE JESOWNER=*` shows all own jobs
- [ ] `SITE JESJOBNAME=TEST*` filters jobs by name pattern
- [ ] `SITE JESSTATUS=OUTPUT` shows only completed jobs
- [ ] `SITE JESSTATUS=ALL` shows all jobs
- [ ] JESINTERFACELEVEL 1: only jobs matching userid pattern are shown
- [ ] JESINTERFACELEVEL 2: all job names visible (with owner filter)
- [ ] `LIST JOBnnnnn` returns spool file list for that specific job
- [ ] Empty result set returns valid (empty) listing, not an error

---

## Step 2.3 — Spool Retrieval & Job Management

### What
`RETR` in JES mode retrieves spool output. `DELE` purges jobs.

### Implementation Details

**ftpd#jes.c — Spool Retrieval:**
- `ftpd_jes_retrieve(sess, jobspec)` — Called when RETR is issued in JES mode:
  - Parse jobspec: `JOBnnnnn` (all spool) or `JOBnnnnn.n` (specific spool file number)
  - Access spool data via crent370's `jes/` module
  - For all-spool retrieval: concatenate spool files, separated by `!! END OF JES SPOOL FILE !!` (z/OS convention)
  - Translate EBCDIC → ASCII for TYPE A
  - Send on data connection
- Error handling:
  - Job not found → `550 JOBnnnnn not found`
  - Spool file number out of range → `550 Spool file n not found for JOBnnnnn`
  - Job not owned by user → `550 Access denied`

**ftpd#jes.c — Job Purge:**
- `ftpd_jes_delete(sess, jobspec)` — Called when DELE is issued in JES mode:
  - Parse job ID from `JOBnnnnn`
  - Purge job via crent370's `jes/` module
  - Reply: `250 JOBnnnnn purged`
- Security: only allow purge of own jobs (or jobs matching JESINTERFACELEVEL rules)

**Command routing in ftpd#cmd.c:**
- When `sess->filetype == FT_JES`:
  - `RETR` → `ftpd_jes_retrieve()`
  - `DELE` → `ftpd_jes_delete()`
  - `LIST` → `ftpd_jes_list()` (Step 2.2)
  - `STOR` → `ftpd_jes_submit()` (Step 2.1)
  - `CWD`, `PWD`, `MKD`, `RMD`, `RNFR`, `RNTO`, `APPE` → `502 Command not available in JES mode`

### Dependencies
- Steps 2.1 + 2.2
- crent370: `jes/` module (spool read, job purge)

### Acceptance Criteria
- [ ] `RETR JOBnnnnn` retrieves all spool output for the job
- [ ] `RETR JOBnnnnn.1` retrieves only the first spool file
- [ ] `RETR JOBnnnnn.3` retrieves the third spool file
- [ ] Multiple spool files separated by `!! END OF JES SPOOL FILE !!`
- [ ] `DELE JOBnnnnn` purges the job from JES queues
- [ ] Cannot retrieve/delete jobs owned by other users → `550`
- [ ] Non-existent job → `550 JOBnnnnn not found`
- [ ] CWD/MKD/RMD in JES mode → `502`
- [ ] Zowe CLI `zowe zftp submit` + `zowe zftp view spool` workflow works end-to-end (manual test)
- [ ] zos-node-accessor `submitJCL()` + `getJobLog()` workflow works (manual test)

---

## Phase 2 Completion Criteria

The overall test for JES interface:

1. `SITE FILETYPE=JES` → `200`
2. `PUT myjob.jcl` → `250-It is known to JES as JOB00042`
3. Wait for job completion
4. `SITE JESOWNER=*` → `200`
5. `LIST` → job listing showing JOB00042 with STATUS=OUTPUT
6. `LIST JOB00042` → spool file listing (JESMSGLG, JESJCL, SYSPRINT, etc.)
7. `GET JOB00042.1` → JESMSGLG content
8. `GET JOB00042` → all spool files concatenated
9. `DELE JOB00042` → `250 JOB00042 purged`
10. `SITE FILETYPE=SEQ` → back to dataset mode, `LIST` shows datasets
