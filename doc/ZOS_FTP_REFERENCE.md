# z/OS FTP Server Behavior Reference

**Source:** Protocol captures from z/OS 3.1 FTP server (IBM FTP CS 3.2)
**Purpose:** Implementation reference for FTPD (MVS 3.8j FTP server)
**Date:** 2026-03-25
**Status:** Living document

---

## Table of Contents

1. [Connection and Authentication](#1-connection-and-authentication)
2. [System Information Commands](#2-system-information-commands)
3. [Working Directory and Navigation](#3-working-directory-and-navigation)
4. [Transfer Parameters](#4-transfer-parameters)
5. [Data Connection Setup](#5-data-connection-setup)
6. [Directory Listings (LIST / NLST)](#6-directory-listings-list--nlst)
7. [File Transfer (RETR / STOR / APPE)](#7-file-transfer-retr--stor--appe)
8. [File Management (DELE / RNFR / RNTO / MKD / RMD)](#8-file-management-dele--rnfr--rnto--mkd--rmd)
9. [SITE Commands](#9-site-commands)
10. [JES Interface](#10-jes-interface)
11. [Status and Informational Commands](#11-status-and-informational-commands)
12. [Pre-Authentication Behavior](#12-pre-authentication-behavior)
13. [Error Responses](#13-error-responses)
14. [Translation and Encoding](#14-translation-and-encoding)
15. [Filesystem Mode Switching](#15-filesystem-mode-switching)

---

## 1. Connection and Authentication

### 1.1 Banner (220)

The server sends a multiline 220 response on connection. Continuation lines
use `220-`, the final line uses `220 ` (space).

```
220-TCPFTP1 IBM FTP CS 3.2 at <hostname>, <time> on <date>.
220-...welcome text...
220 Connection will close if idle for more than 600 seconds.
```

Key fields:

| Field | Value | Notes |
|-------|-------|-------|
| Server name | `TCPFTP1` | Configurable server instance name |
| Software | `IBM FTP CS 3.2` | Communications Server FTP |
| Idle timeout | 600 seconds (10 min) | Announced in final banner line |

### 1.2 USER

```
USER <userid>
331 Send password please.
```

Always returns 331 regardless of whether the userid exists (no user enumeration).

### 1.3 PASS

On success, multiline 230 response:

```
230-MIKEG1 is logged on.  Working directory is "MIKEG1.".
230 <userid> is logged on.  Working directory is "<userid>.".
```

Key observations:

- The working directory shown includes a **trailing dot** after the userid
- The format is: `"<HLQ>."` (double-quoted, with trailing dot)
- The initial working directory prefix is the user's default HLQ

### 1.4 QUIT

```
QUIT
221 Quit command received. Goodbye.
```

### Implementation Notes for MVS 3.8j FTPD

- Banner should include server name, software identification, timestamp, and idle
  timeout value. Use a configurable server name from `SYS1.PARMLIB(FTPDPM00)`.
- Match the `331` / `230` response format exactly for client compatibility.
- The trailing dot in the working directory prefix is significant -- it serves as
  the qualifier separator when appending dataset names. Clients expect this format.
- Do NOT reveal whether a userid exists or not in the 331 response.

---

## 2. System Information Commands

### 2.1 SYST

**Response changes based on current filesystem mode:**

| Mode | Response |
|------|----------|
| MVS (dataset mode) | `215 MVS is the operating system of this server. FTP Server is running on z/OS.` |
| USS (HFS mode) | `215 UNIX is the operating system of this server. FTP Server is running on z/OS.` |

### 2.2 FEAT

```
FEAT
211 no Extensions supported
```

Single-line response. No feature list. This particular z/OS server had SIZE and
MDTM disabled:

```
SIZE <file>
501 command aborted -- FTP server not configured for SIZE/MDTM
```

### 2.3 HELP

Multiline `214-` response listing all supported commands:

```
214-The following commands are recognized:
  USER  PASS  *ACCT *ALLO *SMNT  ABOR  APPE  CDUP  CWD   DELE
  FEAT  HELP  LANG  LIST  MDTM   MKD   MODE  NLST  NOOP  OPTS
  PASS  PASV  PORT   PWD  QUIT   REIN   REST  RETR   RMD  RNFR
  RNTO  SITE  SIZE  SYST  STAT   STOR  STOU  STRU  TYPE  USER
  ADAT  AUTH   CCC  PBSZ  PROT   EPSV  EPRT
214 HELP command successful.
```

Commands prefixed with `*` are recognized but not implemented:

| Unimplemented | Reason |
|---------------|--------|
| `ACCT` | Account information not used |
| `ALLO` | Pre-allocation not needed |
| `SMNT` | Structure mount not supported |

`HELP SITE` returns an extensive (approximately 250-line) multiline response
describing all SITE subcommands with usage text.

### 2.4 NOOP

```
NOOP
200 OK
```

### Implementation Notes for MVS 3.8j FTPD

- **SYST must be mode-aware.** When in UFS mode (via UFSD), respond with `215 UNIX`.
  When in MVS dataset mode, respond with `215 MVS`. Our concept document originally
  specified a static `215 MVS` response -- this needs to change.
- For SYST, replace `FTP Server is running on z/OS` with
  `FTP Server is running on MVS.` or similar to identify our server.
- **FEAT decision:** z/OS returns no features. We could either match this (return
  `211 no Extensions supported`) or list the features we actually support (SIZE,
  REST STREAM, etc.) per RFC 2389. Listing features is more standards-compliant
  and helps modern clients. Recommend listing features unless compatibility testing
  shows problems.
- HELP output should list our supported commands. Use `*` prefix for recognized
  but unimplemented commands.

---

## 3. Working Directory and Navigation

### 3.1 PWD

Response format varies by mode:

| Context | Response |
|---------|----------|
| MVS prefix | `257 "'MIKEG1.'" is working directory.` |
| MVS PDS | `257 "'TRASH.MIKEG1.FTPPROBE.PDS'" partitioned data set is working directory.` |
| Empty prefix | `257 "''" is working directory.` |
| USS | `257 "/tmp/ftpprobe" is the HFS working directory.` |

Format details:

- MVS: `257 "'{prefix}'" is working directory.` -- double-quote wrapping
  single-quote wrapping the DSN prefix (with trailing dot)
- MVS PDS: adds `partitioned data set` before `is working directory`
- USS: no single quotes around path, says `HFS working directory`
- Empty prefix: `"''"` (double-quote, two single-quotes, double-quote)

### 3.2 CWD

#### MVS Mode

| Command | Response | Behavior |
|---------|----------|----------|
| `CWD 'DSN'` | `250 "DSN." is the working directory name prefix.` | Sets prefix, adds trailing dot |
| `CWD 'PDS.NAME'` (actual PDS) | `250 The working directory "PDS.NAME" is a partitioned data set` | Detects PDS, different message |
| `CWD TEST` (unqualified) | `250 "MIKEG1.TEST." is the working directory name prefix.` | Prepends current prefix |
| `CWD 'NONEXISTENT'` | `250 "NONEXISTENT." is the working directory name prefix.` | **No I/O -- always succeeds** |
| `CWD ..` | `250 "" is the working directory name prefix.` | Removes last qualifier |

**Critical behavior:** CWD to a nonexistent dataset name succeeds with code 250.
CWD only sets the name prefix -- it does **not** verify the dataset exists. The
exception is when CWD targets an actual PDS, in which case the response text
changes to indicate "partitioned data set."

#### USS Mode

| Command | Response |
|---------|----------|
| `CWD /` | `250 HFS directory / is the current working directory` |
| `CWD /tmp` | `250 HFS directory /tmp is the current working directory` |

#### Mode Switching via CWD

| Command | Effect |
|---------|--------|
| `CWD /` or `CWD /path` | Enters USS mode from MVS |
| `CWD 'DSN.'` from USS | Returns to MVS mode |

### 3.3 CDUP

Removes one qualifier from the current MVS prefix:

```
PWD → "'MIKEG1.TEST.SUB.'"
CDUP → 250 "" is the working directory name prefix.
       (actually removes one qualifier at a time)
PWD → "'MIKEG1.TEST.'"
```

At empty prefix, CDUP returns `250` with empty prefix (no error).

In USS mode, CDUP moves to parent directory.

### Implementation Notes for MVS 3.8j FTPD

- **CWD does not verify dataset existence.** This is important -- it means CWD
  is a pure prefix manipulation, not a catalog lookup. Only when the prefix
  resolves to an actual PDS does the response text change.
- The trailing dot convention is significant. The prefix `MIKEG1.` means
  that `RETR FOO` resolves to `MIKEG1.FOO`.
- CDUP at empty prefix is not an error. Return `250` with empty prefix.
- CWD `..` is equivalent to CDUP in MVS mode.
- For UFS mode (via UFSD), follow USS conventions: real path validation,
  `HFS directory` in response text.
- PDS detection on CWD: when the resolved name is a PDS in the catalog,
  change the response text. This is the one case where CWD does I/O.

---

## 4. Transfer Parameters

### 4.1 TYPE

| Command | Response | Notes |
|---------|----------|-------|
| `TYPE A` | `200 Representation type is Ascii NonPrint` | Default |
| `TYPE A N` | `200 Representation type is Ascii NonPrint` | Explicit NonPrint |
| `TYPE I` | `200 Representation type is Image` | Binary |
| `TYPE E` | `200 Representation type is Ebcdic NonPrint` | EBCDIC text |
| `TYPE E N` | `200 Representation type is Ebcdic NonPrint` | Explicit NonPrint |
| `TYPE L 8` | `200 Local byte 8, representation type is Image` | Maps to Image |
| `TYPE E T` | `504-TYPE has unsupported format T` | Telnet format rejected |
|            | `504 Type remains Ebcdic NonPrint` | |
| `TYPE X` | `501-unknown type  X` | Unknown type |
|          | `501 Type remains Image` | Reports current type |

Error responses are multiline: first line states the error, final line reports
the type that remains in effect.

### 4.2 STRU

| Command | Response | Notes |
|---------|----------|-------|
| `STRU F` | `250 Data structure is File` | Default |
| `STRU R` | `250 Data structure is Record` | Record mode |
| `STRU P` | `504-Page structure not implemented` | |
|          | `504 Data structure remains Record` | Reports current |

### 4.3 MODE

| Command | Response | Notes |
|---------|----------|-------|
| `MODE S` | `200 Data transfer mode is Stream` | Default |
| `MODE B` | `200 Data transfer mode is Block` | Accepted |
| `MODE C` | `200 Data transfer mode is Compressed` | Accepted |

**z/OS accepts MODE B and MODE C.** These are relevant for record-oriented
transfers and restart support.

### 4.4 Transfer Size Comparison

Measured transfer sizes for the same FB/80 dataset with 10 records (800 bytes raw):

| TYPE | Size (bytes) | Format | Notes |
|------|-------------|--------|-------|
| A (ASCII) | 819 | EBCDIC-to-ASCII translated, trailing blanks removed, CRLF line endings | 80 - trailing blanks + 2 bytes CRLF per record |
| I (Image) | 800 | Raw EBCDIC, no conversion, no separators | Exact 10 x 80 bytes |
| E (EBCDIC) | 810 | EBCDIC with X'15' (NL) separator between records | 10 x 80 + 10 x 1 NL |

**Critical finding -- TYPE E record separator:** TYPE E uses EBCDIC NL (X'15')
as the record separator. This is **not** CRLF and **not** X'25' (EBCDIC LF).
The NL character X'15' appears after each 80-byte record.

### 4.5 STRU R (Record Structure)

With STRU R active (TYPE A), the same 10-record FB/80 dataset transfers as
822 bytes:

- Records are full 80 bytes (trailing blanks **included**, unlike TYPE A + STRU F)
- Record boundaries marked per RFC 959:
  - `X'FF01'` = EOR (End of Record) after each record
  - `X'FF02'` = EOF (End of File) at end of transfer
- Data content is ASCII-translated (TYPE A applies)
- Size: 10 x 80 data + 10 x 2 EOR markers + 1 x 2 EOF marker = 822

### 4.6 SITE RDW (Record Descriptor Word)

With `SITE RDW` active and `TYPE I`:

| Dataset Format | Effect |
|----------------|--------|
| FB (Fixed Block) | **No effect** -- transfer identical to TYPE I without RDW (800 bytes) |
| VB (Variable Block) | Adds 4-byte LLBB (length) prefix per record |

RDW is only meaningful for VB datasets. For FB datasets, records have
fixed length and no RDW is prepended.

### Implementation Notes for MVS 3.8j FTPD

- **TYPE E separator:** Use X'15' (EBCDIC NL) as record separator in TYPE E
  transfers. Not CRLF, not X'25'.
- **TYPE A trailing blank removal:** In TYPE A with STRU F, trailing blanks
  are stripped from each record before adding CRLF. In STRU R, trailing blanks
  are preserved.
- **TYPE L 8:** Accept and treat as synonym for TYPE I.
- **MODE B and MODE C:** z/OS supports both. For Phase 1, support MODE S only
  and return `504` for B and C. Add MODE B in a later phase if restart support
  is needed (REST requires non-stream mode per z/OS: see error responses).
- **STRU R:** Implement RFC 959 record markers (X'FF01' EOR, X'FF02' EOF).
  This is important for clients that need record boundary preservation.
- **RDW:** Only relevant for VB datasets. Prepend 4-byte LLBB (LL = record
  length including RDW, BB = 0) for each record when SITE RDW is active.

---

## 5. Data Connection Setup

### 5.1 PASV

```
PASV
227 Entering Passive Mode (h1,h2,h3,h4,p1,p2)
```

Port is computed as: `p1 * 256 + p2`.

Sending a second PASV replaces the first passive listener -- no error returned,
the previous listener is silently closed.

### 5.2 EPSV (Extended Passive)

```
EPSV
229 Entering Extended Passive Mode (|||port|)
```

### 5.3 PORT

Standard `PORT h1,h2,h3,h4,p1,p2` format:

```
PORT 192,168,1,100,4,1
200 Port request OK.
```

### 5.4 EPRT (Extended Port)

```
EPRT |1|192.168.1.100|1025|
200 EPRT request OK
```

Format: `|protocol|address|port|` where protocol 1 = IPv4.

### Implementation Notes for MVS 3.8j FTPD

- Implement PASV and PORT for Phase 1.
- EPSV and EPRT are IPv4/IPv6-aware extensions. Since we do not support IPv6,
  these are low priority. However, some modern clients send EPSV by default.
  Consider implementing EPSV for IPv4 compatibility.
- On second PASV: close previous passive socket before opening new one.
  Do not return an error.

---

## 6. Directory Listings (LIST / NLST)

### 6.1 LIST -- MVS Dataset Level

Response code `125` to start, `250` on completion:

```
125 List started OK
...data on data connection...
250 List completed successfully.
```

#### Header and Format

```
Volume Unit    Referred Ext Used Recfm Lrecl BlkSz Dsorg Dsname
TRA004 3390   2026/03/25  1 6299  FB      80 27920  PO  'TRASH.MIKEG1.FTPPROBE.BIGPDS'
```

| Column | Width | Description |
|--------|-------|-------------|
| Volume | 6 | Volume serial |
| Unit | 4 | Device type (3390) |
| Referred | 10 | Last-referenced date (YYYY/MM/DD) or `**NONE**` |
| Ext | variable | Extent count |
| Used | variable | Tracks used |
| Recfm | variable | Record format (FB, VB, U, etc.) |
| Lrecl | variable | Logical record length |
| BlkSz | variable | Block size |
| Dsorg | variable | Dataset organization (PO, PS, VS, etc.) |
| Dsname | variable | Fully qualified name in single quotes |

Special cases:

| Condition | Display |
|-----------|---------|
| Migrated dataset | `Migrated` in Volume column, remaining fields blank except Dsname |
| Never referred | `**NONE**` in Referred column |

**Important:** In DATASETMODE (default), `LIST 'PDS.NAME'` shows the dataset-level
catalog entry, NOT the PDS members. To list members, CWD into the PDS first,
then issue LIST.

Member-level listing via wildcard is supported:
`LIST 'PDS.NAME(MEMBER*)'` returns member-level output.

### 6.2 LIST -- PDS Member Level

After CWD into a PDS:

```
 Name     VV.MM   Created       Changed      Size  Init   Mod   Id
MEMBER1   01.00 2026/03/25 2026/03/25 09:17    10    10     0 MIKEG1
```

| Column | Description |
|--------|-------------|
| Name | Member name (up to 8 chars) |
| VV.MM | Version/modification level (ISPF statistics) |
| Created | Creation date (YYYY/MM/DD) |
| Changed | Last changed date and time (YYYY/MM/DD HH:MM) |
| Size | Current number of records |
| Init | Initial number of records |
| Mod | Modification count |
| Id | Last modifier's userid |

Note: Members without ISPF statistics may show blanks in VV.MM and
statistics columns.

### 6.3 LIST -- USS

Standard Unix `ls -l` format with a `total N` header:

```
total 3
drwxr-xr-x   6 BPXROOT  SYSPROG     8192 Mar  4 15:11 BETA
-rw-r-----   1 MIKEG1   SYSPROG       52 Mar 25 11:00 data.txt
lrwxrwxrwx   1 BPXROOT  SYSPROG        9 Mar  5  2025 $SYSNAME -> $SYSNAME/
```

| Field | Description |
|-------|-------------|
| Permissions | 10-char: type (d/l/-) + rwx triplets |
| Links | Hard link count |
| Owner | Owner userid |
| Group | Group name |
| Size | File size in bytes |
| Date | `Mon DD HH:MM` (recent) or `Mon DD  YYYY` (older) |
| Name | Filename; symlinks show `name -> target` |

Wildcard LIST shows full path in the name column:
```
-rw-r-----   1 MIKEG1   SYSPROG       52 Mar 25 11:00 /tmp/ftpprobe/data.txt
```

Empty directory: `total 0` (no error).

`LIST -a` fails on z/OS: `550 LIST cmd failed : FSUMA930 /bin/ls: Unknown option -`
(z/OS invokes `/bin/ls` internally for USS listings).

### 6.4 NLST

#### MVS Mode

One name per line. Quoting depends on context:

| Context | Format | Example |
|---------|--------|---------|
| User prefix active | Unquoted, relative | `ANOW.CNTL` |
| Wildcard with quotes | Single-quoted, fully qualified | `'TRASH.MIKEG1.FTPPROBE.BIGPDS'` |

NLST on a PDS (dataset mode) returns the dataset name, not member names.

#### USS Mode

Full path per line:

```
/tmp/ftpprobe/binary.dat
/tmp/ftpprobe/data.txt
```

Empty USS directory: `550 NLST cmd failed.  No files found.` (error, unlike
LIST which returns `total 0`).

### Implementation Notes for MVS 3.8j FTPD

- **LIST MVS format:** Match the z/OS column layout exactly. Many FTP clients
  (FileZilla, WinSCP) parse this format to display dataset attributes.
- **Dataset-level vs. member-level:** Implement the DATASETMODE behavior where
  LIST on a PDS name shows catalog info, not members. Members are listed only
  after CWD into the PDS or via wildcard `LIST 'PDS(MEM*)'`.
- **Migrated datasets:** MVS 3.8j does not have DFHSM/DFSMShsm. Show all
  datasets as non-migrated. If a dataset is not mounted, show appropriate
  volume info from VTOC.
- **ISPF statistics:** MVS 3.8j PDS members may lack ISPF stats. Show blanks
  for VV.MM and statistics columns when not available.
- **USS LIST:** When in UFS mode via UFSD, produce Unix `ls -l` compatible
  output. UFSD provides file metadata; format it to match.
- **NLST quoting:** Match z/OS behavior: unquoted for relative names under
  user prefix, single-quoted for fully-qualified wildcard results.
- **NLST empty directory:** Return `550` in USS mode, `total 0`-style empty
  in MVS mode. This asymmetry is by design on z/OS.
- **Referred date:** MVS 3.8j tracks last-referenced date via DSCB. Use the
  date from FORMAT-1 DSCB field DS1REFD.

---

## 7. File Transfer (RETR / STOR / APPE)

### 7.1 RETR

#### Response Strings

| Situation | Response |
|-----------|----------|
| Success (MVS) | `125 Sending data set DSN.NAME FIXrecfm 80` |
| Completion | `250 Transfer completed successfully.` |
| Success (USS) | `125-Tagged EBCDIC file translated with table built using file system cp=IBM-1141, network transfer cp=IBM-850` (multiline) |
| Not found (dataset) | `550 Data set DSN not found` |
| Not found (member) | `550 Request nonexistent member DSN(MEM) to be sent.` |
| Not found (USS) | `550 Command RETR fails: /path does not exist.` |
| Is directory (USS) | `550 Command RETR fails: /path is a directory.` |

The 125 response for MVS datasets includes the RECFM and LRECL:
`125 Sending data set DSN FIXrecfm 80` (FIX = FB, VARrecfm = VB, etc.).

### 7.2 STOR

| Situation | Response |
|-----------|----------|
| Success | `125 Storing data set DSN.NAME` |
| Completion | `250 Transfer completed successfully.` |
| PDS not created | `550 DSN(MEM) requests a nonexistent partitioned data set.  Use MKD command to create it.` |

Key behaviors:

- Storing to an existing dataset **replaces** its content (no confirmation)
- VB datasets: records delineated by CRLF in TYPE A, each becomes a
  variable-length record
- SITE allocation parameters are **sticky** within a session -- they persist
  across multiple STOR commands until explicitly changed
- When no SITE allocation params are set, server uses its configured defaults
- Storing a PDS member requires the PDS to already exist (use MKD to create it)

### 7.3 APPE

| Situation | Response |
|-----------|----------|
| Success | `125 Appending to data set DSN` |
| Completion | `250 Transfer completed successfully.` |

Key behaviors:

- Works on both sequential datasets and PDS members
- APPE to a **nonexistent** dataset **creates** the dataset (uses current
  SITE allocation parameters)
- APPE to an existing PDS member appends to the member content

### 7.4 SITE TRUNCATE Interaction

| Setting | Behavior | Response |
|---------|----------|----------|
| TRUNCATE active | Records exceeding LRECL are silently truncated | `250 Transfer completed (data was truncated)` |
| TRUNCATE not active | Transfer may fail on overlong records | `451-File transfer failed. File contains records that are longer than the LRECL` |

Note: In some observed cases, the server truncated data even without explicit
SITE TRUNCATE, suggesting a server-default truncation policy may be in effect.

### Implementation Notes for MVS 3.8j FTPD

- **125 response format:** Include RECFM and LRECL information in the 125
  response for MVS datasets. This helps clients understand the transfer format.
  Use `FIXrecfm` for FB, `VARrecfm` for VB, etc.
- **STOR to existing:** Replace content without prompting. This is standard FTP
  behavior and z/OS behavior.
- **APPE creates:** APPE to a nonexistent dataset should create it using current
  allocation defaults. This is z/OS behavior and useful for automation scripts.
- **Sticky SITE params:** Allocation parameters set via SITE persist for the
  entire session. Do not reset between STOR operations.
- **TRUNCATE:** Implement SITE TRUNCATE toggle. When active, silently truncate
  overlong records and include `(data was truncated)` in the 250 response.
  When not active, reject transfers with records exceeding LRECL.

---

## 8. File Management (DELE / RNFR / RNTO / MKD / RMD)

### 8.1 DELE

| Situation | Response |
|-----------|----------|
| Success | `250 DSN deleted.` |
| Not found | `550 DELE fails: DSN does not exist.` |
| Entire PDS (with members) | `250 DSN deleted.` (succeeds, deletes whole PDS) |
| USS directory (non-empty) | `550 DELE fails: /path is a directory and is not empty.` |

### 8.2 RNFR / RNTO

| Situation | Response |
|-----------|----------|
| RNFR success | `350 RNFR accepted. Please supply new name for RNTO.` |
| RNFR not found | `550 RNFR fails: DSN does not exist.` |
| RNTO success | `250 OLD renamed to NEW` |
| RNTO target exists (MVS) | `550 Rename fails: TARGET already exists.` |
| RNTO target exists (USS) | `250` (succeeds -- **overwrites** target!) |
| RNTO without RNFR (USS) | `550 Renaming attempt failed. Rc was 129` |

PDS member rename: `RNFR 'PDS(OLD)'` / `RNTO 'PDS(NEW)'` returns `250`.

**Critical -- RNFR state persistence bug:** On z/OS, after a failed RNFR
(550 response), the **previous** successful RNFR remains pending. If a client
sends RNTO after a failed RNFR, the rename uses the old RNFR source. This
caused unintended renames in testing.

### 8.3 MKD

| Situation | Response |
|-----------|----------|
| MVS (create PDS) | `257 "'DSN'" created.` |
| USS (create dir) | `257 "/path" created.` |
| Already exists | `550 MKDIR failed: EDC5117I File exists.` |
| Parent missing (USS) | `550 MKDIR failed: EDC5129I No such file or directory.` |

### 8.4 RMD

| Situation | Response |
|-----------|----------|
| Success | `250 DSN deleted.` |
| Not found | `550 "DSN" data set does not exist.` |
| Non-empty (USS) | `550 RMDIR failed: EDC5136I Directory not empty.` |

Note: RMD uses the same response text as DELE for MVS datasets (`deleted`).

### Implementation Notes for MVS 3.8j FTPD

- **DELE entire PDS:** z/OS allows deleting a PDS with members via DELE. Our
  FTPD should support this (scratch the dataset via catalog management).
- **RNFR state:** Clear the RNFR pending state on ANY command other than RNTO.
  Do NOT persist RNFR across failed RNFR attempts. The z/OS behavior (stale
  RNFR persists) is arguably a bug -- we should be stricter.
- **USS rename overwrites:** In UFS mode, RNTO to an existing target should
  overwrite (Unix semantics). In MVS mode, reject with 550 if target exists.
- **MKD for MVS:** MKD in MVS mode creates a PDS (allocate via dynalloc with
  DSORG=PO). The allocation parameters come from current SITE settings.
- **RMD for MVS:** Deletes the dataset (PDS). Same as DELE for datasets.
  In USS mode via UFSD, removes directory only if empty.
- **MKD does not create intermediate qualifiers:** Unlike Unix `mkdir -p`,
  MKD in USS mode fails if parent directories do not exist.

---

## 9. SITE Commands

### 9.1 Bare SITE (No Arguments)

```
SITE
202 SITE not necessary; you may proceed
```

Returns 202, not a settings dump. This differs from some FTP server
implementations that show current SITE settings on bare SITE.

### 9.2 Allocation Parameters

All allocation SITE commands return `200 SITE command was accepted`.

| Command | Purpose | Example |
|---------|---------|---------|
| `SITE RECFM=FB` | Record format | FB, VB, U, F, V |
| `SITE LRECL=80` | Logical record length | |
| `SITE BLKSIZE=27920` | Block size | Auto-adjusted if invalid |
| `SITE PRIMARY=100` | Primary allocation (tracks) | |
| `SITE SECONDARY=50` | Secondary allocation (tracks) | |
| `SITE DIRECTORY=20` | PDS directory blocks | |
| `SITE TRACKS` | Allocation units = tracks | |
| `SITE CYLINDERS` | Allocation units = cylinders | |
| `SITE DATACLAS=classname` | SMS data class | Accepted (even without SMS) |
| `SITE STORCLAS=classname` | SMS storage class | Accepted (even without SMS) |

#### BLKSIZE Auto-Adjustment

When BLKSIZE is not a valid multiple of LRECL for RECFM FB:

```
200-BLOCKSIZE must be a multiple of LRECL for RECFM FB
200-BLOCKSIZE being set to 6120
200 SITE command was accepted
```

The server adjusts BLKSIZE downward to the nearest valid multiple.

### 9.3 Transfer Modifiers

| Command | Purpose | Type | Default |
|---------|---------|------|---------|
| `SITE FILETYPE=SEQ` | Sequential dataset mode | Setting | SEQ |
| `SITE FILETYPE=JES` | JES spool interface mode | Setting | SEQ |
| `SITE TRAILING` | Toggle trailing blank removal | Toggle | OFF (blanks kept) |
| `SITE TRUNCATE` | Toggle overlong record truncation | Toggle | OFF |
| `SITE RDW` | Toggle Record Descriptor Word | Toggle | OFF |

Toggle commands: send once to enable, send again to disable.

**SITE TRAILING default:** On the observed z/OS server, trailing blanks are
NOT removed by default (STAT shows "Trailing blanks are not removed").

### 9.4 Unknown Parameters

```
SITE FOOBAR=123
200-Unrecognized parameter 'FOOBAR=123' on SITE command.
200 SITE command was accepted
```

Unknown parameters return `200` (not `502`). The server warns but accepts
the command. This is lenient behavior for forward compatibility.

### 9.5 JESINTERFACELEVEL

```
SITE JESINTERFACELEVEL=1
200-Unrecognized parameter 'JESINTERFACELEVEL=1' on SITE command.
200 SITE command was accepted
```

Treated as unrecognized on this z/OS server. JES interface level may be
controlled by server configuration rather than client SITE command.

### 9.6 Redundant SITE

```
SITE FILETYPE=SEQ    (when already SEQ)
200 SITE command was accepted
```

Returns 200 (not 202). Setting a value to its current value is accepted silently.

### Implementation Notes for MVS 3.8j FTPD

- **Bare SITE:** Return `202 SITE not necessary; you may proceed` to match z/OS.
  Our concept document planned to show current settings on bare SITE -- change
  this to match z/OS. Use STAT for settings display instead.
- **SITE TRAILING default:** z/OS default is blanks NOT removed. Our concept
  assumed trim-by-default. Need to decide: match z/OS (keep blanks) or optimize
  for modern clients (trim blanks). Recommend matching z/OS for compatibility.
- **Unknown SITE params:** Return 200 with warning text, not 502. This matches
  z/OS behavior and is more forgiving for clients that send z/OS-specific params.
- **BLKSIZE auto-adjustment:** Implement BLKSIZE validation for FB datasets.
  If BLKSIZE is not a multiple of LRECL, round down to nearest multiple and
  warn in the response.
- **DATACLAS/STORCLAS:** Accept these commands (return 200) but ignore them.
  MVS 3.8j has no SMS. Accepting silently prevents client errors.
- **FILETYPE=JES:** Implement as mode switch for JES interface (see section 10).
- **Sticky parameters:** All SITE settings persist for the entire session.
  Document this clearly for users.
- **Toggle semantics:** TRAILING, TRUNCATE, RDW are toggles. Track boolean
  state in session, flip on each SITE command for that parameter.

---

## 10. JES Interface

### 10.1 Entering JES Mode

```
SITE FILETYPE=JES
200 SITE command was accepted
```

PWD in JES mode still shows the MVS dataset prefix:

```
PWD
257 "'MIKEG1.'" is working directory.
```

### 10.2 Job Submission (STOR)

```
STOR SUBMIT.JCL
125 Sending Job to JES internal reader FIXrecfm 80
250-It is known to JES as J0887745
250 Transfer completed successfully.
```

The 125 response says "Sending Job to JES internal reader" (not "Storing").
The 250 response includes the assigned JOB ID.

### 10.3 Job Listing (LIST)

```
LIST
125 List started OK for JESJOBNAME=*, JESSTATUS=ALL and JESOWNER=*
```

#### JES LIST Format

Header:
```
JOBNAME  JOBID    OWNER    STATUS CLASS
```

Data rows:
```
MIKEG1   T0886971 MIKEG1   ACTIVE TSU
MIKEG1T  J0887691 MIKEG1   OUTPUT A        (JCL error) 3 spool files
```

| Column | Description |
|--------|-------------|
| JOBNAME | Job name (up to 8 chars) |
| JOBID | Job identifier |
| OWNER | Submitting userid |
| STATUS | ACTIVE, OUTPUT, INPUT |
| CLASS | Output class or TSU/STC |

Additional text after CLASS: error indication, spool file count.

#### Job ID Prefixes

| Prefix | Type |
|--------|------|
| `J0nnnnnn` | Batch jobs |
| `T0nnnnnn` | TSO sessions |
| `S0nnnnnn` | Started tasks |

#### JESENTRYLIMIT

When the result set exceeds the configured limit:

```
250-JESENTRYLIMIT of 200 reached.  Additional entries not displayed
250 List completed successfully.
```

### 10.4 Spool Retrieval (RETR)

#### All Spool Files

```
RETR J0887745
125 Sending all spool files for requested Jobid
```

Multiple spool DDs are separated by:
```
 !! END OF JES SPOOL FILE !!
```

(Space-prefixed, exact string.)

#### Specific Spool DD

```
RETR J0887745.1
125 Sending data set MIKEG1.MIKEG1T.J0887745.D0000002.JESMSGLG
```

DD numbering starts at **1** (not 0). The 125 response includes the full
spool dataset name in the format:
`<owner>.<jobname>.<jobid>.D<seqno>.<ddname>`

### 10.5 Job Purge (DELE)

```
DELE J0887745
250 Cancel successful
```

Response says "Cancel successful" (not "deleted" or "purged").

```
DELE J0099999
550 Jobid J0099999 not found for JESJOBNAME=..., JESSTATUS=..., JESOWNER=...
```

### 10.6 Returning to Dataset Mode

```
SITE FILETYPE=SEQ
200 SITE command was accepted
```

### Implementation Notes for MVS 3.8j FTPD

- **JES mode:** Implement as a session state flag. When FILETYPE=JES, LIST/RETR/STOR/DELE
  operate on JES spool instead of datasets.
- **Job submission:** Use crent370 `jes` module to write JCL to internal reader.
  Return the assigned JOB ID in the 250 response.
- **Job listing:** Use crent370 `jes` module to enumerate jobs. Match the z/OS
  column format for client parsing compatibility.
- **Job ID format:** MVS 3.8j JES2 uses `JOBnnnnn` (5 digits) format, not the
  z/OS `J0nnnnnn` (7 digits) format. Our LIST output should use the native
  MVS 3.8j format.
- **Spool DD separator:** Use the exact string ` !! END OF JES SPOOL FILE !!`
  between DDs when sending all spool files. This is a well-known convention
  that clients depend on.
- **DD numbering:** Start at 1, not 0.
- **JESENTRYLIMIT:** Implement a configurable limit to prevent huge result sets.
  Default to 200 to match z/OS.
- **Purge response:** Use `250 Cancel successful` to match z/OS.

---

## 11. Status and Informational Commands

### 11.1 STAT (No Arguments)

```
STAT
211-FTP server status:
     ...connection info, current settings, allocation defaults, JES config...
211 End of status.
```

Multiline `211-` response including:

- Connection information (client address, port)
- Current TYPE, MODE, STRU settings
- Current SITE allocation defaults
- JES configuration
- Trailing blank / truncation settings
- Transfer codepage information

### 11.2 STAT with Argument

```
STAT 'DSN'
504 STAT file-identifier: not implemented

STAT /path
504 STAT file-identifier: not implemented
```

File-level STAT is not implemented on this z/OS server.

### 11.3 ABOR

```
ABOR
226 Abort successful.
```

When no transfer is in progress, returns 226 (not an error).

### 11.4 REST

```
REST 100
504 Restart requires Block or Compressed transfer mode.
```

REST requires MODE B or MODE C. In MODE S (stream), REST is rejected.

### Implementation Notes for MVS 3.8j FTPD

- **STAT:** Implement no-argument STAT to show session information and current
  settings. This replaces the bare-SITE settings display from our concept.
- **STAT with file:** Return `504 STAT file-identifier: not implemented` for
  Phase 1. Can be implemented later if needed.
- **ABOR:** Return `226` even when no transfer is active. If a transfer is
  active, abort it and return `226`.
- **REST:** For Phase 1 (MODE S only), return `504 Restart requires Block or
  Compressed transfer mode.` Consider REST support when MODE B is implemented.

---

## 12. Pre-Authentication Behavior

### 12.1 Commands Allowed Before Login

| Command | Response Code | Notes |
|---------|--------------|-------|
| `SYST` | 215 | Full response, no auth required |
| `FEAT` | 211 | Full response |
| `HELP` | 214 | Full response |
| `NOOP` | 200 | |
| `STAT` | 211 | Full server status including JES info |
| `SITE` | 200 | SITE commands accepted before login |
| `QUIT` | 221 | |

### 12.2 Commands Rejected Before Login

| Command | Response |
|---------|----------|
| `PWD` | `530 You must first login with USER and PASS.` |
| `LIST` | `530 Not logged in.` |
| `RETR` | `530 Not logged in.` |
| `CWD` | `530 Not logged in.` |
| All data transfer commands | `530 Not logged in.` |

Note: Two different 530 message formats are used:
- `530 You must first login with USER and PASS.` (for some commands)
- `530 Not logged in.` (for others)

### Implementation Notes for MVS 3.8j FTPD

- **SITE before login:** z/OS accepts SITE before authentication, including
  `SITE FILETYPE=JES`. This allows clients to configure transfer mode before
  logging in. We should allow this for compatibility.
- **STAT before login:** Exposing full server status (including JES config)
  before auth is a security consideration. We may want to return a reduced
  STAT response before authentication.
- **SYST/FEAT/HELP:** These are informational and safe to allow pre-auth.
- **530 messages:** Use `530 Not logged in.` as the default rejection message
  for unauthenticated commands.

---

## 13. Error Responses

### 13.1 Complete Error Response Catalog

| Code | Situation | Response Text |
|------|-----------|---------------|
| 500 | Unknown command | `500 unknown command XYZZY` |
| 501 | Invalid dataset name | `501 Invalid data set name "'DSN'".  Use MVS Dsname conventions.` |
| 501 | Unknown type | `501-unknown type  X` / `501 Type remains <current>` |
| 504 | Unsupported TYPE format | `504-TYPE has unsupported format T` / `504 Type remains <current>` |
| 504 | Unsupported STRU | `504-Page structure not implemented` / `504 Data structure remains <current>` |
| 504 | STAT with file | `504 STAT file-identifier: not implemented` |
| 504 | REST in stream mode | `504 Restart requires Block or Compressed transfer mode.` |
| 530 | Not logged in (variant 1) | `530 You must first login with USER and PASS.` |
| 530 | Not logged in (variant 2) | `530 Not logged in.` |
| 550 | Dataset not found | `550 Data set DSN not found` |
| 550 | Member not found | `550 Request nonexistent member DSN(MEM) to be sent.` |
| 550 | Name too long | `550 Name length error for pathname 'DSN'` |
| 550 | File not found (USS) | `550 Command RETR fails: /path does not exist.` |
| 550 | Is a directory (USS) | `550 Command RETR fails: /path is a directory.` |
| 550 | RNFR not found | `550 RNFR fails: DSN does not exist.` |
| 550 | Rename target exists (MVS) | `550 Rename fails: TARGET already exists.` |
| 550 | RNTO without RNFR (USS) | `550 Renaming attempt failed. Rc was 129` |
| 550 | DELE not found | `550 DELE fails: DSN does not exist.` |
| 550 | DELE USS directory | `550 DELE fails: /path is a directory and is not empty.` |
| 550 | RMD not found | `550 "DSN" data set does not exist.` |
| 550 | RMD non-empty (USS) | `550 RMDIR failed: EDC5136I Directory not empty.` |
| 550 | MKD exists | `550 MKDIR failed: EDC5117I File exists.` |
| 550 | MKD parent missing (USS) | `550 MKDIR failed: EDC5129I No such file or directory.` |
| 550 | PDS required for STOR | `550 DSN(MEM) requests a nonexistent partitioned data set.  Use MKD command to create it.` |
| 550 | Job not found | `550 Jobid J0099999 not found for JESJOBNAME=..., JESSTATUS=..., JESOWNER=...` |
| 550 | USS LIST -a | `550 LIST cmd failed : FSUMA930 /bin/ls: Unknown option -` |
| 550 | NLST empty (USS) | `550 NLST cmd failed.  No files found.` |
| 451 | LRECL exceeded | `451-File transfer failed. File contains records that are longer than the LRECL` |
| 501 | SIZE/MDTM disabled | `501 command aborted -- FTP server not configured for SIZE/MDTM` |

### 13.2 Multiline Error Patterns

Several error responses use multiline format (code followed by `-`, then
final line with code and space):

```
504-TYPE has unsupported format T
504 Type remains Ebcdic NonPrint

501-unknown type  X
501 Type remains Image

200-Unrecognized parameter 'FOOBAR=123' on SITE command.
200 SITE command was accepted
```

The continuation lines provide context; the final line provides the
definitive status.

### Implementation Notes for MVS 3.8j FTPD

- Match z/OS error response text as closely as practical. Clients and
  automation scripts may parse these strings.
- Use multiline responses for errors where additional context helps
  (TYPE/STRU rejections, SITE warnings).
- The `EDC5xxx` error codes in USS responses are z/OS Language Environment
  messages. For UFS errors via UFSD, use our own error descriptions.
- For MVS dataset errors, keep the z/OS format: `550 Data set DSN not found`
  (not `550 File not found`).

---

## 14. Translation and Encoding

### 14.1 Observed Codepage Configuration

The z/OS server reported:
```
SBDataconn codeset names: IBM-1141,IBM-850
```

| Codepage | Usage | Description |
|----------|-------|-------------|
| IBM-1141 | Host (EBCDIC) | German CECP variant of IBM-1047 |
| IBM-850 | Network (ASCII) | Western European Latin-1 (DOS) |

### 14.2 TYPE E Record Separator

TYPE E transfers use EBCDIC NL (X'15') as the record separator, inserted
after each record. This is the EBCDIC New Line character, distinct from:

| Character | Hex | Name | Used For |
|-----------|-----|------|----------|
| NL | X'15' | New Line (EBCDIC) | TYPE E record separator |
| LF | X'25' | Line Feed (EBCDIC) | Not used as separator |
| CR | X'0D' | Carriage Return (EBCDIC) | Not used as separator |
| CRLF | (ASCII 0D 0A) | CR+LF | TYPE A line endings |

### 14.3 USS File Tagging

USS files on z/OS are "tagged" with their codepage. The FTP server uses
the file tag to determine the translation table:

```
125-Tagged EBCDIC file translated with table built using file system cp=IBM-1141, network transfer cp=IBM-850
```

### Implementation Notes for MVS 3.8j FTPD

- **Codepage:** Our FTPD should use IBM-1047 (standard US EBCDIC for open
  systems) as the host codepage, not IBM-1141 (which is German-specific).
  The network codepage should be ISO 8859-1 (Latin-1) or US-ASCII.
- **Translation table:** The `ftpd#xlt.c` module provides the EBCDIC/ASCII
  translation tables. These must map correctly between IBM-1047 and the
  chosen ASCII variant. Key differences from IBM-1141:
  - IBM-1141 maps differently for: `[`, `]`, `{`, `}`, `|`, `\`, and other
    national characters
  - IBM-1047 is the standard choice for MVS open systems software
- **TYPE E NL:** Use X'15' as the record separator in TYPE E transfers.
  This must be in the `ftpd#xlt.c` module as a defined constant.
- **UFS file tags:** MVS 3.8j UFS (via UFSD) does not have z/OS-style file
  tagging. All UFS files should be treated as untagged and translated using
  the server's default codepage pair.

---

## 15. Filesystem Mode Switching

### 15.1 Mode Transitions

The server maintains a current filesystem mode (MVS or USS) per session:

| Trigger | From | To | Response |
|---------|------|----|----------|
| `CWD /` | MVS | USS | `250 HFS directory / is the current working directory` |
| `CWD /path` | MVS | USS | `250 HFS directory /path is the current working directory` |
| `CWD 'DSN.'` | USS | MVS | `250 "DSN." is the working directory name prefix.` |
| `SITE FILETYPE=JES` | any | JES | `200 SITE command was accepted` |
| `SITE FILETYPE=SEQ` | JES | MVS | `200 SITE command was accepted` |

### 15.2 State Preservation

- After entering JES mode and returning to SEQ, the previous USS path is
  preserved and accessible
- MVS prefix and USS path are maintained independently in the session

### 15.3 Commands Affected by Mode

| Command | MVS Mode | USS Mode | JES Mode |
|---------|----------|----------|----------|
| LIST | Dataset catalog listing | Unix ls -l | Job listing |
| NLST | Dataset names | File paths | Job names |
| RETR | Read dataset/member | Read file | Read spool |
| STOR | Write dataset/member | Write file | Submit job |
| DELE | Scratch dataset | Delete file | Purge job |
| CWD | Set DSN prefix | Change directory | N/A |
| PWD | Show DSN prefix | Show path | Show DSN prefix |
| MKD | Allocate PDS | Create directory | N/A |
| RMD | Scratch PDS | Remove directory | N/A |
| SYST | `215 MVS...` | `215 UNIX...` | `215 MVS...` |

### Implementation Notes for MVS 3.8j FTPD

- **Three-mode state machine:** The session must track MVS, UFS, and JES modes.
  Store both MVS prefix and UFS path independently so switching between modes
  preserves context.
- **UFS mode requires UFSD:** If UFSD is not running, `CWD /` should return
  `550 UFS service not available` rather than silently entering a broken mode.
- **JES + PWD:** In JES mode, PWD still shows the MVS dataset prefix (not a
  JES-specific response). Match this behavior.
- **SYST in JES mode:** Returns MVS response (not JES-specific).

---

## Appendix A: Response Code Summary

Quick reference of all observed response codes and their usage:

| Code | Category | Usage |
|------|----------|-------|
| 125 | Transfer starting | Data connection open, transfer beginning |
| 200 | Command OK | TYPE, MODE, PORT, EPRT, SITE, NOOP |
| 202 | Superfluous | Bare SITE (no arguments) |
| 211 | System status | STAT, FEAT |
| 214 | Help | HELP |
| 215 | System type | SYST |
| 220 | Service ready | Connection banner |
| 221 | Closing | QUIT |
| 226 | Transfer complete | ABOR (idle) |
| 227 | Passive mode | PASV |
| 229 | Extended passive | EPSV |
| 230 | Logged in | PASS (success) |
| 250 | Action completed | CWD, CDUP, STRU, DELE, RNTO, RMD, transfer complete, JES purge |
| 257 | Path created | PWD, MKD |
| 331 | Need password | USER |
| 350 | Pending further | RNFR |
| 451 | Local error | Transfer failed (LRECL exceeded) |
| 500 | Syntax error | Unknown command |
| 501 | Argument error | Invalid name, unknown type |
| 504 | Not implemented | Unsupported parameter/feature |
| 530 | Not logged in | Command requires authentication |
| 550 | Action failed | Not found, permission denied, etc. |

---

## Appendix B: Default Server Configuration (from STAT)

Observed defaults on the z/OS 3.1 test server:

| Setting | Default Value |
|---------|---------------|
| RECFM | FB |
| LRECL | 80 |
| BLKSIZE | 27920 |
| Trailing blanks | Not removed |
| FILETYPE | SEQ |
| Idle timeout | 600 seconds |
| JESENTRYLIMIT | 200 |
| Host codepage | IBM-1141 |
| Network codepage | IBM-850 |
