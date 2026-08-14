# FTPD RAKF Setup

FTPD has no user database of its own. Every `USER`/`PASS` is verified by RAKF,
and every data set access is checked against RAKF under the **logged-in user's**
identity. Without RAKF nobody can log in, and no configuration option turns that
off.

This guide covers what FTPD needs. It is not a RAKF manual — the authoritative
one is the RAKF User's Guide that ships with RAKF itself.

## Which RAKF this assumes

**RAKF 1.2.x**, which is what TK4-, TK5 and MVS/CE ship. There is **no
administration tooling** at that level: the tables are plain text members you
edit by hand and then reload. Where RAKF 2.0.0 changes something, this guide
says so in a box.

> Some systems (MVS/CE) add a `RAKFCL` command processor that wraps table
> editing in RACF-like syntax. It is a distribution extension, not part of RAKF,
> and nothing here depends on it.

---

## 1. The two tables

Everything lives in two members, both plain 80-column text:

| Member | Holds |
|--------|-------|
| `SYS1.SECURE.CNTL(USERS)` | who exists, their group and password |
| `SYS1.SECURE.CNTL(PROFILES)` | what is protected and who may reach it |

Both are **column-positioned**. There is no keyword syntax — a line that looks
like a RACF command is just a wrong line.

### USERS

```
         1         2         3
1234567890123456789012345678901234
IBMUSER  ADMIN   *SYS1     Y
```

| Columns | Field |
|:-------:|-------|
| 1 – 8 | userid |
| 10 – 17 | group |
| 18 | `*` if the userid has more than one group entry |
| 19 – 26 | password |
| 28 | Operations authority, `Y` or `N` |
| 31 – 50 | comment |

### PROFILES

```
         1         2         3         4         5         6
123456789012345678901234567890123456789012345678901234567890123456
DATASET SYS1.SECURE.*                               RAKFADM UPDATE
```

| Columns | Field |
|:-------:|-------|
| 1 – 8 | class — `DATASET `, `FACILITY`, `DASDVOL ` |
| 9 – 52 | resource or data set name, generic `*` allowed |
| 53 – 60 | group — **blank means universal access** |
| 61 – 66 | permission — `NONE`, `READ`, `UPDATE`, `ALTER` |

### Three rules that are easy to miss

- **Both tables must be in ascending sort order.** A sort error does not skip
  the bad line, it *inhibits initialization of the table* — so one misplaced
  entry takes down authorization for the whole system, not just for FTPD.
- **The universal entry comes first.** The table is searched from the bottom
  upwards so the most specific hit wins, which only works if the blank-group
  line for a resource precedes its group-specific lines.
- **A line starting with `*` is a comment.** RAKF's own shipped table writes the
  equivalent RACF command above each block as a comment. It is worth copying
  that habit — it is the only place the intent is readable.

### Reloading

After **any** change to either member:

```
S RAKF
```

That is the whole procedure, on every system. There is no MODIFY command; RAKF
reads the tables when it starts. A successful users-table load reports:

```
RAKFUIDS4  USER TABLE UPDATED
```

A rejected one echoes the offending card and terminates:

```
RAKFUIDS2  INPUT DATA INVALID OR OUT OF SEQ.
FTPD     FTPD     ******** N                                            00000706
RAKFUIDSX  ** PROGRAM TERMINATED **
```

**Read that echoed line with care.** The password column is always printed as
`********`, whatever it really contained — including empty. So the one field
that most often causes the rejection is the one field the diagnostic will not
show you, and a bad card looks indistinguishable from a good one. When the line
looks correct, check the password column and the sort order, in that order.

Nothing is updated when this happens: the in-core table keeps its **previous**
contents, which presents as "my change had no effect" rather than as a failure.

---

## 2. The FTPD user

Add to `SYS1.SECURE.CNTL(USERS)`:

```
         1         2         3
1234567890123456789012345678901234
FTPD     USER     Kf7Qw2Rt N
```

- **group `USER`** — FTPD requests this group by name when it switches identity
  at startup. Whatever you put in columns 10–17 is *not* what it ends up with:
  RAKF does not check the connection at RACINIT, so the group FTPD asks for is
  the group it gets. Keeping them the same avoids a table that says one thing
  while the console says another.
- **A password is required.** RAKF 1.2.x rejects the line at reload if columns
  19–26 are blank, and the reload then fails as a whole.
- **Operations authority `N`** — see [section 5](#5-why-ftpd-switches-identity).
  This one character is load-bearing.

> **Pick a real password here.** FTPD never uses it — it signs on with
> `PASSCHK=NO` — but the account is a normal RAKF user, so anyone who knows the
> password can log in over FTP as `FTPD`. Treat it as a credential, not a
> placeholder, and do not permit `FTPD` on `FTPAUTH` in the next section.

> **RAKF 2.0.0** blanks the password column and keeps the credential in
> `SYS1.SECURE.SHADOW`. There, `ADDUSER FTPD DFLTGRP(USER)` does this step and a
> blank column 19–26 is correct — the exact opposite of 1.2.x. Check which
> release you are on before copying either form.

Then `S RAKF`.

---

## 3. FTPAUTH — who may use FTP at all

`FTPAUTH` in the `FACILITY` class is the gate FTPD checks after the password is
verified. Add to `SYS1.SECURE.CNTL(PROFILES)`:

```
         1         2         3         4         5         6
123456789012345678901234567890123456789012345678901234567890123456
*
* rdefine facility ftpauth uacc(none)
* permit ftpauth cl(facility) id(ftpuser) access(read)
*
FACILITYFTPAUTH                                             NONE
FACILITYFTPAUTH                                     FTPUSER READ
```

The first line denies everyone, the second grants one group. Order matters —
universal first, as above.

**Define this profile.** It is tempting to skip it, because FTPD lets everyone
in when it is absent — but that is not a permissive default, it is RAKF's
behaviour of granting **ALTER to undefined resources**. Skipping it also means
the `FTPD` account from section 2 can log in over FTP.

### Which group

`FTPUSER` above is a dedicated group holding exactly the users who should have
FTP access — the tighter arrangement, and the reason to prefer it is that FTP
access then has its own switch instead of riding on a group that exists for
other reasons.

If you would rather let every ordinary user in, permit the shared group instead:

```
FACILITYFTPAUTH                                     USER    READ
```

Either way, do not permit `FTPD` itself.

---

## 4. Data set access

FTPD issues a RACHECK against the `DATASET` class before every data set
operation, under the **client's** identity:

| FTP command | Access checked |
|-------------|----------------|
| `RETR` | READ |
| `STOR`, `APPE` | UPDATE |
| `DELE` | ALTER |
| `MKD`, `RMD` | ALTER |
| `RNFR` / `RNTO` | ALTER |

A common arrangement gives each user full control of their own HLQ and read
access to the system libraries:

```
         1         2         3         4         5         6
123456789012345678901234567890123456789012345678901234567890123456
*
* addsd 'IBMUSER.*'                 uacc(none)
* permit 'IBMUSER.*' id(ibmuser)    access(alter)
*
DATASET IBMUSER.*                                           NONE
DATASET IBMUSER.*                                   IBMUSER ALTER
```

### How FTPD reads the answer

`racf_auth()` returns the SAF return code, and FTPD acts on it:

| RC | Meaning | FTPD |
|----|---------|------|
| 0 | a profile permits the access | allow |
| 4 | **no profile covers the resource** | allow |
| 8 | a profile refuses the access | deny — `550` / `530` |

RC 4 is an allow, not a refusal: it is the same answer data set OPEN, IDCAMS and
the catalog act on, so refusing it would make FTP the only path on the system
that cannot reach an unprotected data set.

Note what that means in practice. RAKF grants **ALTER** to anything no profile
covers, so on a system with no `DATASET` profiles at all, every logged-in user
can do anything to any data set — and FTPD is simply reporting that faithfully.
The stock MVS/CE and TK4- configurations ship a `DATASET *` catch-all, which is
why RC 4 never occurs for the DATASET class there.

---

## 5. Why FTPD switches identity

`ICHSFR00` assigns work that carries no userid of its own:

| Type | USERID | GROUP |
|------|--------|-------|
| Batch jobs | `PROD` | `PRDGROUP` |
| Started tasks | `STC` | `STCGROUP` |

**The `STC` userid has Operations authority, hardcoded in `ICHSFR00`.**
Operations means "always allow access unless explicitly denied by a rule" — so a
started task that does nothing about it can reach every data set on the system.

FTPD therefore performs an inline RACINIT at startup and drops from `STC` to its
own identity:

```
FTPD004I STC IDENTITY SET TO FTPD/USER VIA RACINIT
```

That is what the `N` in column 28 of the `USERS` line is for: the identity FTPD
lands on must not carry Operations authority, or the switch achieves nothing.

If the RACINIT fails, FTPD reports it and **keeps running under `STC`**:

```
FTPD004W RACINIT ENVIR=CREATE FAILED RC=nn
```

Do not treat that as cosmetic. It means the server is running with Operations
authority on every data set.

### Security invariant — `FTPD/USER` must stay least-privilege

> **This is an invariant, not a recommendation.**
>
> `FTPD/USER` **must not** be granted broad data set authority — no
> Operations-equivalent access, no wide `ID(FTPD) ACCESS(ALTER)` profiles. Its
> only required authority is what the started task itself needs to run, never
> access to user data. Per-user access is authorised against the **logged-in
> user's** identity.
>
> **Why it is an invariant:** the per-command ABEND recovery handler resets the
> address-space-wide security environment (`ASXBSENV`) to the `FTPD/USER` ACEE
> after an unexpected ABEND. Sessions run as concurrent subtasks sharing that
> one field, so a concurrent session can be transiently pulled onto
> `FTPD/USER`. That is fail-closed **only while `FTPD/USER` is minimally
> privileged**: a session pulled onto it can lose authority, never gain it. Give
> `FTPD/USER` broad authority and the same transient switch becomes
> **fail-open** — a concurrent session's data set OPEN could momentarily
> authorise with elevated access.

---

## 6. Verify

1. `S RAKF` — expect `RAKFUIDS4  USER TABLE UPDATED`.
2. `/S FTPD` — expect `FTPD004I STC IDENTITY SET TO FTPD/USER VIA RACINIT`.
3. Log in over FTP with an ordinary userid that is in the permitted group, and
   with one that is not. The second must be refused with `530`.

Step 3 is the one worth doing. Steps 1 and 2 only prove the tables loaded and
FTPD changed identity; they say nothing about whether `FTPAUTH` is actually
gating anything.

---

## 7. Troubleshooting

| Symptom | Cause |
|---------|-------|
| `RAKFUIDS2  INPUT DATA INVALID OR OUT OF SEQ.` | A malformed line, or the table is not in ascending sort order. The utility terminates and the in-core table keeps its **previous** contents — which looks exactly like "my change had no effect". The echoed card masks the password as `********`, so it cannot tell you whether that was the problem |
| Reload rejected and the echoed card looks fine | Most likely a blank password. RAKF 1.2.x requires columns 19–26 to be filled — section 2 |
| `RAKF008W illegal operation -- access denied` | The `RAKFADM` FACILITY profile denies the table-update utility |
| `FTPD004W RACINIT ENVIR=CREATE FAILED` | The `FTPD` user is not in `USERS`, or the table was never reloaded. FTPD is running under `STC` with Operations authority |
| `FTPD003W APF SETUP FAILED RC=n` | No APF authorisation and no SVC 244. The RACINIT above cannot work either |
| `530 Login incorrect` for a valid user | Wrong password, or the user is not in `USERS`. A user added without `S RAKF` does not exist yet |
| `530 Not authorized for FTP access` | `FTPAUTH` denies this user — check the group in columns 53–60 |
| `550 Access denied to <dsn>` | A `DATASET` profile refuses the access under the **client's** identity, not FTPD's |
| Everyone can reach everything | No `DATASET` profiles. Undefined resources get ALTER — section 4 |

---

## Related

- [installation.md](https://github.com/mvslovers/ftpd/blob/main/doc/installation.md)
  — installing FTPD itself
- [FTPD_CONCEPT.md](https://github.com/mvslovers/ftpd/blob/main/doc/FTPD_CONCEPT.md)
  — architecture, including the session/identity model
