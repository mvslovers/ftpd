# FTPD RAKF Setup

FTPD has no user database of its own. Every `USER`/`PASS` is verified by RAKF,
and every data set access is checked against RAKF under the **logged-in user's**
identity. Without RAKF nobody can log in, and no configuration option turns that
off.

This guide covers what FTPD needs. It is not a RAKF manual — the authoritative
one is the RAKF User's Guide that ships with RAKF itself.

## Which RAKF this assumes

**RAKF 1.2.x** and **RAKF 2.0.0** (the release `v2.0.0` of the RAKF
repository). At 1.2.x there is **no administration tooling**: the tables are
plain text members you edit by hand and then reload. RAKF 2.0.0 keeps the two
tables and adds TSO commands for them (`ADDUSER`, `ALTUSER`, `DELUSER`,
`LISTUSER`, `ADDSD`, `RDEFINE`, `PERMIT`, `RDELETE`), a password file
`SYS1.SECURE.SHADOW`, and the mapping of a started task by its name
(section 2). Where RAKF 2.0.0 changes something, this guide says so in a box.

> **Do not edit `PROFILES` by hand; RAKF's maintainer recommends the
> commands.** Create profiles with `RDEFINE`, `PERMIT` and `ADDSD`, then reload
> with `S RAKFPROF`. The commands keep the file sorted, refuse a group line that
> has no universal line, and do not write a profile twice. A hand-edited file
> that is wrong aborts the reload (`U0300`, `U0400`, `U0500`) and, on RAKF
> 2.0.0, keeps JES2 from starting at the next IPL.
>
> Wherever this guide shows lines of `PROFILES`, they are what the commands
> write, so that you can check the result. A RAKF 1.2.x has no such commands;
> for it the reference at the end of section 7 is all there is.

**FTPD 1.3.0 does not need RAKF 2.0.0 to run.** RAKF itself, for the login, is
required; the release matters for one thing only. **Letting users see the jobs
of other users (section 7) needs RAKF 2.0.0 or later**, the release whose class
table contains `JESSPOOL`. On a RAKF 1.2.x FTPD installs and starts, and every
user sees, reads and cancels only the own jobs; section 7 says what RAKF writes
to the console in that case.

The identity that the started task `FTPD` runs under must be in a group that
has READ on `FACILITY SVC244`, such as `STARTED` in the shipped `users.txt`. Otherwise FTPD
cannot obtain APF authorization (`RAKF000A` for `FACILITY SVC244`,
`FTPD003W APF SETUP FAILED RC=4`). Section 2 says which identity the task gets.

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
like a RACF command is just a wrong line. Create the profiles with the RAKF
commands ([PROFILES](#profiles), below), not by writing lines into the member.

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
| 30 | Special authority, `Y` or `N` — RAKF 2.0.0 only, see the box below |
| 31 – 50 | comment |

> **RAKF 2.0.0:** column 30 is the SPECIAL authority. A user needs it to run
> `ADDUSER`, `ALTUSER`, `DELUSER`, `LISTUSER`, `ADDSD`, `RDEFINE`, `PERMIT` and
> `RDELETE`; RAKF turns it on only for `Y`, so a blank column means no. A
> `USERS` table that you keep from an earlier RAKF has no entries in column 30:
> before you use these commands, put a `Y` in column 30 of every line of the
> administrator's entries and reload (`S RAKF`). The commands cannot do this
> for you, because they need the flag themselves. The password column 19 – 26
> is not used; the password is kept as a hash in `SYS1.SECURE.SHADOW`.

### PROFILES

> **Do not edit `PROFILES` by hand; RAKF's maintainer recommends the
> commands.** Create profiles with `RDEFINE`, `PERMIT` and `ADDSD`, then reload
> with `S RAKFPROF`. The commands keep the file sorted, refuse a group line that
> has no universal line, and do not write a profile twice. A hand-edited file
> that is wrong aborts the reload (`U0300`, `U0400`, `U0500`) and, on RAKF
> 2.0.0, keeps JES2 from starting at the next IPL.

#### Create profiles with the RAKF commands

```
RDEFINE resource CLASS(class) UACC(access)        resource of any class but DATASET
ADDSD   'dsname.*' UACC(access)                   resource of the class DATASET (alias AD)
PERMIT  resource CLASS(class) ID(group) ACCESS(access)       (alias PE)
S RAKFPROF
```

`access` is `NONE`, `READ`, `UPDATE`, `CONTROL` or `ALTER`. For a data set
profile `PERMIT` needs no `CLASS`.

- `RDEFINE` and `ADDSD` write the universal line of a profile, `PERMIT` the
  line for one group. For each profile the universal line comes first.
- `RDEFINE` on a profile that exists answers `RDEFINE: Profile already defined`
  and changes nothing. `PERMIT` for a resource without a universal line answers
  `PERMIT: No universal access record found` and writes nothing. A second
  `PERMIT` for the same group and resource replaces the access instead of
  writing a second line.
- The commands put the lines in order, and they remove the comment lines from
  the member.
- The commands change the member only. Nothing is in effect before the reload;
  `S RAKFPROF` answers `RAKFPROF7  RAKF PROFILES UPDATED`.
- Run them in TSO under a userid with the SPECIAL authority (see the box under
  USERS) that is connected to the group `RAKFADM`. In a batch TSO step
  (`PGM=IKJEFT01`) **start every command in column 1**: with a blank in front,
  `RDEFINE` is run as `ADDSD` and creates a `DATASET` profile.

Section 7 has the details and the examples for the class `JESSPOOL`.

#### The layout of the member — to read a file, not to write one

This is how an existing `PROFILES` is read, and how a broken one is repaired
(section 7, "Writing `PROFILES` by hand"). It is not how profiles are created.

```
         1         2         3         4         5         6
123456789012345678901234567890123456789012345678901234567890123456
DATASET SYS1.SECURE.*                               RAKFADM UPDATE
```

| Columns | Field |
|:-------:|-------|
| 1 – 8 | class — `DATASET `, `FACILITY`, `DASDVOL `, `JESSPOOL` |
| 9 – 52 | resource or data set name, generic `*` allowed |
| 53 – 60 | group — **blank means universal access** |
| 61 – 68 | permission — `NONE`, `READ`, `UPDATE`, `CONTROL`, `ALTER` |

#### Three rules that are easy to miss — for a hand-written file

The commands keep these rules for you.

- **Both tables must be in ascending sort order.** A sort error does not skip
  the bad line, it *inhibits initialization of the table* — so one misplaced
  entry takes down authorization for the whole system, not just for FTPD. The
  profile loader of RAKF 2.0.0 sorts its input itself and accepts lines in any
  order; the users table has to be in order.
- **The universal entry comes first.** The table is searched from the bottom
  upwards so the most specific hit wins, which only works if the blank-group
  line for a resource precedes its group-specific lines.
- **A line starting with `*` is a comment.** The commands remove such lines.

### Reloading

After **any** change to either member:

```
S RAKF
```

After a change of `PROFILES` alone, `S RAKFPROF` is enough.

That is the whole procedure, on every system. There is no MODIFY command; RAKF
reads the tables when it starts. A successful users-table load reports:

```
RAKFUIDS4  USER TABLE UPDATED
```

A rejected one echoes the offending card and terminates:

```
RAKFUIDS2  INPUT DATA INVALID OR OUT OF SEQ.
JSMITH   USER     ******** N                                            00000706
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

## 2. The identity of the FTPD started task

FTPD does not choose its own identity. It runs under whatever RAKF assigned to
the started task when it was created, and keeps that as its resting identity
for as long as it lives. Section 5 explains why that identity matters.

What RAKF assigns depends on the release:

| RAKF | Identity of the started task `FTPD` |
|------|-------------------------------------|
| 2.0.0 | the userid in `SYS1.SECURE.CNTL(USERS)` that is named like the started procedure, `FTPD`, with the group and the Operations flag of its entry; `STC` / `STCGROUP` if there is no such userid |
| 1.2.x and any other without a mapping by STC name | `STC` / `STCGROUP`, hardcoded in `ICHSFR00` |

For a release other than these, the way a mapping is written is part of that
release — see its documentation. FTPD needs two things from it:

- **Map the name `FTPD` to an identity with Operations authority `N`.** This one
  character is load-bearing, see [section 5](#5-why-the-started-tasks-identity-matters).
- **Keep that identity least-privilege.** It is an invariant, not a
  recommendation, and it is spelled out in section 5.

If the identity is a userid in `SYS1.SECURE.CNTL(USERS)`, it is a normal RAKF
account like any other: RAKF 1.2.x requires a password in columns 19–26 and
rejects the whole reload if it is blank, and anyone who knows that password can
log in over FTP as that user. Pick a real password, treat it as a credential,
and do not permit the userid on `FTPAUTH` in the next section. On RAKF 2.0.0
the credential lives in `SYS1.SECURE.SHADOW` and the column is blank — check
which release you are on before copying either form.

After any change to the tables, `S RAKF`.

> **RAKF 2.0.0: the group of the `FTPD` user has to be allowed SVC 244.** At
> startup FTPD asks for its authorization with SVC 244, and it does so under
> the identity of the started task. SVC 244 checks the
> group of that identity against the `FACILITY` profile `SVC244`. If the group
> is refused, FTPD starts and reports `FTPD003W APF SETUP FAILED`.
>
> - In the `PROFILES` that come with RAKF 2.0.0, `SVC244` has the universal
>   entry `NONE` and READ for the groups `ADMIN`, `STARTED` and `STCGROUP`.
>   Check your own table: it is the one that counts.
> - **Do not use `STCGROUP` for the user.** In that same table `STCGROUP` has
>   ALTER on `DATASET *` and `DASDVOL *`, which is the broad authority that
>   section 5 forbids. RAKF's own seed files put the `FTPD` user into the group
>   `STARTED`, which holds nothing but READ on `SVC244`.
> - A group of your own needs its own `SVC244` line. Add it with the command
>   (do not edit `PROFILES` by hand; RAKF's maintainer recommends the
>   commands), then reload with `S RAKFPROF`:
>
>   ```
>   PERMIT SVC244 CLASS(FACILITY) ID(STARTED) ACCESS(READ)
>   ```
>
>   The line it writes is `FACILITYSVC244` and the group `STARTED` with `READ`
>   in columns 61 and on.
> - The user is created with `ADDUSER FTPD PASSWORD(pw) DFLTGRP(STARTED)`.
>   Do not code `OPERATIONS`. `LISTUSER FTPD` shows the authority it got.
>   Both commands need the SPECIAL authority (section 1). Then `S RAKF`.

> **Upgrading from FTPD 1.2.0 or earlier:** those releases logged on as
> `FTPD`/`USER` themselves at startup. FTPD 1.3.0 does not. On a RAKF without a
> mapping by STC name the started task now runs as `STC`/`STCGROUP` again, with
> Operations authority; section 5 says what that means. On RAKF 2.0.0 the
> `FTPD` line in `USERS` is used again, now as the mapping: its group decides
> the identity of the started task and has to be allowed SVC 244 (the box
> above). The documentation of FTPD 1.2.0 says `ADDUSER FTPD DFLTGRP(USER)`.
> FTPD 1.2.0 asks for SVC 244 before it logs on as `FTPD`/`USER`, so on RAKF
> 2.0.0 the group in the `FTPD` line of `USERS` is the group that is checked,
> and `USER` is not one of the groups that the shipped `SVC244` profile allows.
> Give the `FTPD` user a group that holds `SVC244`, such
> as `STARTED`, also if you stay on 1.2.0.

---

## 3. FTPAUTH — who may use FTP at all

`FTPAUTH` in the `FACILITY` class is the gate FTPD checks after the password is
verified. It has to exist in `SYS1.SECURE.CNTL(PROFILES)`, and the group of your
FTP users has to be allowed to read it.

**Do not edit `PROFILES` by hand; RAKF's maintainer recommends the commands.**
Define the profile and permit the group, then reload:

```
RDEFINE FTPAUTH CLASS(FACILITY) UACC(NONE)
PERMIT FTPAUTH CLASS(FACILITY) ID(FTPUSER) ACCESS(READ)
S RAKFPROF
```

`RDEFINE` writes the universal line, which denies everyone. `PERMIT` grants one
group. Run both in TSO under a userid with the SPECIAL authority
(section 1), each command starting in column 1.

**You need not look first whether the profile is there.** MVS/CE ships
`FTPAUTH` with the universal entry `NONE` and the group `ADMIN` at `READ`. On
such a system `RDEFINE` answers `RDEFINE: Profile already defined` and changes
nothing, and `PERMIT` is the only command that does work:

```
RDEFINE FTPAUTH CLASS(FACILITY) UACC(NONE)
RDEFINE: Profile already defined
PERMIT FTPAUTH CLASS(FACILITY) ID(USER) ACCESS(READ)
PERMIT:  access READ    granted to FACILITY/FTPAUTH
PERMIT: ID = USER
PERMIT: Run RAKFPROF to activate the changes
```

After `S RAKFPROF` has answered `RAKFPROF7  RAKF PROFILES UPDATED`, `PROFILES`
has these lines for `FTPAUTH` (here with the shipped `ADMIN` line and the group
`USER`). They are what the commands wrote, shown so that you can check the
result:

```
         1         2         3         4         5         6
123456789012345678901234567890123456789012345678901234567890123456
FACILITYFTPAUTH                                             NONE
FACILITYFTPAUTH                                     ADMIN   READ
FACILITYFTPAUTH                                     USER    READ
```

The first line denies everyone, the others grant one group each. The commands
put the group lines of one resource in ascending group order. A second
universal line for `FTPAUTH` is what a hand-edited file gets wrong: it makes the
reload end with `RAKFPROF3  I/P RECORDS OUT OF ORDER` and the abend U0300
(section 7).

**The profile must exist.** It is tempting to skip it, because FTPD lets everyone
in when it is absent — but that is not a permissive default, it is RAKF's
behaviour of granting **ALTER to undefined resources**. Skipping it also means
the userid of the started task from section 2 can log in over FTP.

### Which group

`FTPUSER` above is a dedicated group holding exactly the users who should have
FTP access — the tighter arrangement, and the reason to prefer it is that FTP
access then has its own switch instead of riding on a group that exists for
other reasons.

If you would rather let every ordinary user in, permit the shared group instead:

```
PERMIT FTPAUTH CLASS(FACILITY) ID(USER) ACCESS(READ)
```

Either way, do not permit the userid of the FTPD started task itself.

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
access to the system libraries. Create the profile with the commands (do not
edit `PROFILES` by hand; RAKF's maintainer recommends the commands):

```
ADDSD 'IBMUSER.*' UACC(NONE)
PERMIT 'IBMUSER.*' ID(IBMUSER) ACCESS(ALTER)
S RAKFPROF
```

`ADDSD` answers `ADDSD  : DATASET  profile defined: IBMUSER.*` and `PERMIT`
answers `PERMIT:  access ALTER   granted to DATASET /IBMUSER.*`. `PROFILES`
then has these lines:

```
         1         2         3         4         5         6
123456789012345678901234567890123456789012345678901234567890123456
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

## 5. Why the started task's identity matters

`ICHSFR00` assigns work that carries no userid of its own:

| Type | USERID | GROUP |
|------|--------|-------|
| Batch jobs | `PROD` | `PRDGROUP` |
| Started tasks | `STC` | `STCGROUP` |

**The `STC` userid has Operations authority, hardcoded in `ICHSFR00`.**
Operations means "always allow access unless explicitly denied by a rule" — so a
started task that keeps this default can reach every data set on the system
whenever a check is made against the started task rather than a user.

FTPD no longer replaces the identity it starts with. At startup it reports the
one it has:

```
FTPD004I STC RUNS AS <user>/<group>
```

On a RAKF that maps the STC name to a low-privilege identity, that is the
identity you configured. On a RAKF without such a mapping it is
`STC/STCGROUP`, and the server runs with Operations authority:

```
FTPD004I STC RUNS AS STC/STCGROUP
```

FTPD cannot tell the two situations apart by itself — read the message. If it
names `STC`, either upgrade RAKF or accept that the started task is as
privileged as the default account.

If the address space has no security environment at all, FTPD says so:

```
FTPD004W NO SECURITY ENVIRONMENT FOR THE STC: RAKF PERMITS ANY ACCESS WITHOUT AN ACEE
```

Do not treat that as cosmetic. RAKF reads a missing ACEE as "access permitted",
so nothing FTPD does outside a user's session is checked.

### Security invariant — the started task's identity must stay least-privilege

> **This is an invariant, not a recommendation.**
>
> The identity of the started task **must not** be granted broad data set
> authority — no Operations-equivalent access, no wide `ID(FTPD) ACCESS(ALTER)`
> profiles. Its only required authority is what the started task itself needs to
> run, never access to user data. Per-user access is authorised against the
> **logged-in user's** identity.
>
> **Why it is an invariant:** after every operation that runs under a user's
> identity, and after an unexpected ABEND in a session, FTPD puts the
> address-space-wide security environment (`ASXBSENV`) back to the identity the
> started task started with. Sessions run as concurrent subtasks sharing that
> one field, so a concurrent session can be transiently pulled onto it. That is
> fail-closed **only while that identity is minimally privileged**: a session
> pulled onto it can lose authority, never gain it. Give it broad authority —
> which is exactly what `STC/STCGROUP` is — and the same transient switch
> becomes **fail-open**: a concurrent session's data set OPEN could momentarily
> authorise with elevated access.

---

## 6. Verify

1. `S RAKF` — expect `RAKFUIDS4  USER TABLE UPDATED`.
2. `/S FTPD` — expect `FTPD004I STC RUNS AS` followed by the identity you
   configured for the started task. `STC/STCGROUP` there means no mapping
   applies (section 5).
3. Log in over FTP with an ordinary userid that is in the permitted group, and
   with one that is not. The second must be refused with `530`.

Step 3 is the one worth doing. Steps 1 and 2 only prove the tables loaded and
which identity the started task has; they say nothing about whether `FTPAUTH` is actually
gating anything.

---

## 7. JESSPOOL — who may see and delete other users' jobs

FTPD 1.3.0 decides for every job whether the logged-in user may list it, read
its output and delete it. This applies to the JES mode of FTP (`SITE FILETYPE=JES`:
`LIST`, `NLST`, `RETR`, `DELE`). The RAKF class `JESSPOOL` holds the rules.
RAKF 2.0.0 introduced that class: FTPD runs without it, but then no user can
see the jobs of others, and a profile is the only way to allow it.

> **FTPD always shows, reads and deletes your own jobs; a profile of the class
> JESSPOOL regulates what you may see and delete of the jobs of others.**
> A JESSPOOL profile never restricts a user's own jobs; it only decides what a
> user may see and delete of other users' jobs.

The profiles regulate the FTP channel only. They say nothing about who can
read the spool through TSO, SDSF or another interface.

Without any JESSPOOL profile nothing else is visible: every user sees, reads and
deletes only their own jobs, and `SITE JESOWNER=*` or another user's id is
refused with `501`. That is the state of FTPD 1.3.0 without any action.

A refused check writes no message to the operator console. FTPD asks RAKF
through libc370's `racf_auth()`, which sets the request flag LOG=NONE, and
RAKF's `ICHSFR00` then skips `RAKF0005`/`RAKF000A`. FTPD's own
`STDOUT` log records what it refused: a `JES <op> denied: ...` line for each
job hidden from `dir JOBnnnnn`, `RETR` and `DELE`, and a `SITE JESOWNER
refused` line. A plain `dir` hides jobs without a line of their own; the
number is in the `JES LIST: ... hidden ...` line. The User's Guide, "A
Complete Sample Session with JESSPOOL", has a full session and the measurement.

### What FTPD asks RACF

For a job of another owner FTPD builds the entity

```
<owner>.<jobname>.<jobid>          for example   ABC.PAYROLL.JOB00042
```

and asks RACF for READ on it in the class `JESSPOOL` (and, for `DELE`, for
UPDATE). The owner comes first because RAKF compares only the first **8**
characters of an entity: with the owner first, a profile `OWNER.*` is a rule
about a user. A job without an owner has the entity `.<jobname>.<jobid>`.

| Access to the entity | `LIST`, `LIST JOBnnnnn`, `RETR` | `DELE` |
|----------------------|----------------------------------|--------|
| no profile covers it, or `NONE` | the job is hidden | hidden |
| `READ` | listed and read | `550 Access denied - cannot cancel <jobid>` |
| `UPDATE`, `CONTROL`, `ALTER` | listed and read | `250 Cancel successful` |

A hidden job is answered with the same `550 Jobid <id> not found for
JESJOBNAME=…, JESSTATUS=…, JESOWNER=…` as a job that does not exist, so the
reply does not tell whether the job exists.

`SITE JESOWNER=<value>` only narrows the view, never widens it. The value is one
user id (compared whole: `ABC` does not match `ABCD`) or `*` for all owners.
`*` and another user's id need READ on the profile that would cover that
user's jobs; without it the answer is `501`. A pattern such as `ABC*`, a value
of more than eight characters and a character outside `A–Z 0–9 $ # @` are `501`
as well; a value is never cut to eight characters.

> **Change from FTPD 1.2.0:** `SITE JESOWNER=` compared a prefix and accepted any
> value (`200`), and any user could list, read and delete the jobs of everyone.
> If you relied on seeing or deleting other users' jobs, define the profiles
> below **before** you upgrade.

### Rules for profiles

1. **Write an owner profile only where an owner gives something to others or
   takes it away, and always with the star: `OWNER.*`** (for example `ABC.*`).
   Your own jobs need no profile. Always write the owner profile with the
   star, `OWNER.*`. Do not rely on a profile without a star in the class
   JESSPOOL: prefix matching is designed for DATASET only.
2. **At most one `*` profile**, if you mean all owners. No `OWN*` next to
   `OWN.*`, no profile that names job name characters (`ABC.PROD*`).
3. **Why:** among several matching profiles the **last** one in EBCDIC order
   wins, not the longest. `OWN*` beats `OWN.*` (`.` sorts before `*`), and a
   `*` profile loses against every more specific profile, except for owners
   that sort before `*`: the empty owner (`.`) and a user id that starts with
   `$`. For a user id that starts with `$` there is no effective owner profile
   against a `*` profile: the profile for `$ABC` sorts before `*` and loses
   against it. (Derived from the EBCDIC collating order, not measured.)
4. **Look first** whether a generic JESSPOOL profile exists. A second line with
   the same key, or a duplicate line, aborts the reload (`U0300`, below).
5. Job name and job ID make no difference (only eight characters count). There
   is no profile per job.
6. **Required line: `SYSTEM.*` with `NONE` next to every universal reader
   line.** All started tasks run under the owner `SYSTEM`; as soon as a
   universal line grants READ, the logs of all started tasks, including that
   of FTPD, are open to every reader. Nothing enforces this line. Check under
   which owner your own started-task logs are listed before you rely on it, and
   do not create a user `SYSTEM` (it would then count as the owner of those
   jobs and see them whatever the profiles say).
7. **Every entity that has a line with an id needs a line without an id (the
   universal access) for the same entity: `OWNER.*`, `SYSTEM.*` and `*`
   itself.** See the warning below for what happens otherwise.
8. **Put the administrators' line into every owner block.** As soon as an owner
   profile exists, a role that should see that owner's jobs needs a line in it:
   a block without it falls back to its own universal access (`NONE`) and, as
   the last matching profile, wins against `*`.
9. **One connected group is enough.** The right comes from any group the user
   is connected to in `USERS` (the default group included), not from
   OPERATIONS. A requested but not connected group gives nothing. FTPD
   requests no group at login. In the class JESSPOOL the OPERATIONS attribute
   grants no access; it does so only in the classes DATASET, TAPEVOL and
   DASDVOL (as in IBM RACF).
10. **The prefix trap:** `MIKE1.*` does not match `MIKE10.JOBE.JOB00016` (the
    prefix of the profile is `MIKE1.`); that job falls to `*`. Likewise
    `MIKE1` is not the owner of the jobs of `MIKE10` and vice versa.
11. **The owner needs no line.** Their own jobs are decided before RACF.
12. **Jobs without an owner** fall under `*`. A reader of `*` (READ) sees them,
    and no profile can exclude them (`.` sorts before `*`); `SYSTEM.*` with
    `NONE` does not cover them.
13. **Every user id that occurs as the owner of jobs and may log in over FTP
    lists, reads and deletes its own jobs, whatever the JESSPOOL profiles
    say.** To keep such a user id out, deny it the FTP login (FACILITY
    `FTPAUTH`), not the spool. When you create a user id, check that it is not
    already the owner of jobs.
14. **The owner of a job is chosen by the submitting client; the JESSPOOL view
    of a job is not audit evidence.**

Do not raise the JESSPOOL entity maximum above 8 without adjusting the FTPD key
(`FTPD_JWN_KEYLEN` in the source).

### Create the profiles with the RAKF commands

> **Do not edit `PROFILES` by hand; RAKF's maintainer recommends the
> commands.** Create profiles with `RDEFINE`, `PERMIT` and `ADDSD`, then reload
> with `S RAKFPROF`. The commands keep the file sorted, refuse a group line that
> has no universal line, and do not write a profile twice. A hand-edited file
> that is wrong aborts the reload (`U0300`, `U0400`, `U0500`) and, on RAKF
> 2.0.0, keeps JES2 from starting at the next IPL.

Create the profiles with the RAKF commands `RDEFINE` and `PERMIT` and reload the
in-core tables with `S RAKFPROF`. The commands come with RAKF 2.0.0.

```
RDEFINE 'resource' CLASS(class) UACC(NONE|READ|UPDATE|CONTROL|ALTER)      (alias RDEF)
PERMIT  'resource' CLASS(class) ID(group) ACCESS(NONE|READ|UPDATE|CONTROL|ALTER)   (alias PE)
```

- **Start every command in column 1.** In a TSO batch step a command with a
  leading blank is not run as written: ` RDEFINE ...` is run as `ADDSD` and
  creates a `DATASET` profile, and ` PERMIT ...` for a profile that does not
  exist answers `PERMIT: No universal access record found`.
- **Who may run them:** the user must be connected to the group `RAKFADM` and
  should have the SPECIAL authority (RAKF 2.0.0 `HELP` for `RDEFINE` and
  `PERMIT`: "You must be connected to the RAKFADM group." and "The issuing user
  should have the SPECIAL authority. Update access to the SYS1.SECURE.* dataset
  is necessary or the issuing user should have the OPERATIONS authority along
  with the SPECIAL authority."). SPECIAL is column 30 of `USERS` (section 1).
- `RDEFINE` creates the universal line of a profile, `PERMIT` adds a line for
  a group. `PERMIT` is refused for a profile without a universal line
  (`PERMIT: No universal access record found`), so the trap of rule 7 does not
  exist when you use the commands. For each
  profile `RDEFINE` comes before `PERMIT`.
- **A reload is part of every sequence.** Without `S RAKFPROF` the profile is
  not in effect. A successful reload is acknowledged on the console with
  `RAKFPROF7  RAKF PROFILES UPDATED`. Profile changes apply from the next
  command of a running session; a changed group membership applies after a new
  login.
- The commands sort the records and remove comment lines from `PROFILES`;
  comments in the file are lost with the first command. They accept up to
  1495 (`RDEFINE`) or 1500 (`PERMIT`) records (from the RAKF source, not
  measured), fewer than the loader (2000).
- `RDEFINE` refuses an existing profile ("Profile already defined"); `PERMIT`
  replaces the access of an existing combination of class, profile and id
  instead of writing a second line (from the RAKF source, not measured).
- The class must be known to the running RAKF; an unknown class is refused with
  RC 8 (`Class … unknown to RAKF`).

Keep a copy of `PROFILES` before the first change.

### Examples

Each example is a sequence of commands; the lines below it show what `PROFILES`
contains afterwards. Test 18 of FTPD runs each of these sequences on RAKF 2.0.0
and compares the lines.

**A. Auditors read all jobs except the started tasks; everyone else only their
own.**

```
RDEF * CL(JESSPOOL) UACC(NONE)
PE * CL(JESSPOOL) ID(AUDIT) ACCESS(READ)
RDEF SYSTEM.* CL(JESSPOOL) UACC(NONE)
S RAKFPROF
```

```
         1         2         3         4         5         6
123456789012345678901234567890123456789012345678901234567890123456
JESSPOOL*                                                   NONE
JESSPOOL*                                           AUDIT   READ
JESSPOOLSYSTEM.*                                            NONE
```

`*` `NONE` closes foreign jobs for everyone, AUDIT reads foreign jobs (READ, no
delete) except the started-task logs (`SYSTEM.*` `NONE`). Everyone keeps their
own jobs. Jobs without an owner fall under `*`: an auditor can read them and
they cannot be excluded.

**B. Everyone reads everything, only OPS deletes.**

```
RDEF * CL(JESSPOOL) UACC(READ)
PE * CL(JESSPOOL) ID(OPS) ACCESS(UPDATE)
RDEF SYSTEM.* CL(JESSPOOL) UACC(NONE)
S RAKFPROF
```

```
JESSPOOL*                                                   READ
JESSPOOL*                                           OPS     UPDATE
JESSPOOLSYSTEM.*                                            NONE
```

Everyone also deletes their own jobs. **The line `SYSTEM.*` `NONE` is
required** (rule 6): without it all started-task logs are open to every reader.
It closes them for OPS as well; to open them for OPS add `SYSTEM.*` with the id
`OPS` and `READ` right behind it. Jobs without an owner are readable for
everyone and cannot be excluded.

**C. Nothing.** Create no profile: everyone sees, reads and deletes only their
own jobs.

**D. Everyone sees and deletes their own jobs; the group ADMIN sees and deletes
all.**

```
RDEF * CL(JESSPOOL) UACC(NONE)
PE * CL(JESSPOOL) ID(ADMIN) ACCESS(UPDATE)
S RAKFPROF
```

```
JESSPOOL*                                                   NONE
JESSPOOL*                                           ADMIN   UPDATE
```

Members of ADMIN (one connected group is enough, rule 9) see, read and delete all
foreign jobs, the started-task logs and jobs without an owner included. If
ADMIN should not get the started-task logs, add `SYSTEM.*` `NONE`; without an
ADMIN line in that block ADMIN is shut out as well (rule 8).

**E. Everyone reads everything except the started-task logs; ADMIN deletes;
everyone deletes their own.**

```
RDEF * CL(JESSPOOL) UACC(READ)
PE * CL(JESSPOOL) ID(ADMIN) ACCESS(UPDATE)
RDEF SYSTEM.* CL(JESSPOOL) UACC(NONE)
S RAKFPROF
```

```
JESSPOOL*                                                   READ
JESSPOOL*                                           ADMIN   UPDATE
JESSPOOLSYSTEM.*                                            NONE
```

`SYSTEM.*` `NONE` is required (rule 6) and closes the started-task logs for
ADMIN as well; if the administrators should read them, add `SYSTEM.*` with the
id `ADMIN` and `UPDATE`. Jobs without an owner are readable for everyone.

**F. A pair, MIKE1 and MIKE2, may read and delete each other's jobs; ADMIN all;
otherwise everyone sees only their own.**

```
RDEF * CL(JESSPOOL) UACC(NONE)
PE * CL(JESSPOOL) ID(ADMIN) ACCESS(UPDATE)
RDEF MIKE1.* CL(JESSPOOL) UACC(NONE)
PE MIKE1.* CL(JESSPOOL) ID(ADMIN) ACCESS(UPDATE)
PE MIKE1.* CL(JESSPOOL) ID(MIKE2) ACCESS(UPDATE)
RDEF MIKE2.* CL(JESSPOOL) UACC(NONE)
PE MIKE2.* CL(JESSPOOL) ID(ADMIN) ACCESS(UPDATE)
PE MIKE2.* CL(JESSPOOL) ID(MIKE1) ACCESS(UPDATE)
S RAKFPROF
```

```
JESSPOOL*                                                   NONE
JESSPOOL*                                           ADMIN   UPDATE
JESSPOOLMIKE1.*                                             NONE
JESSPOOLMIKE1.*                                     ADMIN   UPDATE
JESSPOOLMIKE1.*                                     MIKE2   UPDATE
JESSPOOLMIKE2.*                                             NONE
JESSPOOLMIKE2.*                                     ADMIN   UPDATE
JESSPOOLMIKE2.*                                     MIKE1   UPDATE
```

`ID()` takes a user id here, although the `HELP` text speaks of a group (not
verified beyond the test images); the commands accepted it there and wrote the
lines shown. The ADMIN line in both blocks is
required (rule 8); `MIKE1.*` does not cover the jobs of `MIKE10` (rule 10).

### The order of the lines and the placement in `profiles.txt`

The loader sorts the lines itself, so their order does not matter. Keeping them
in ascending order makes the file easier to read and duplicate lines easier to
see. In the `profiles.txt` that ships with RAKF the classes are in the order
`DASDVOL`, `DATASET`, `FACILITY`, `TAPEVOL`, `TERMINAL`; `JESSPOOL` sorts between
`FACILITY` and `TAPEVOL`.

### Writing `PROFILES` by hand — reference only

**Do not edit `PROFILES` by hand; RAKF's maintainer recommends the commands.**
This part is for reading a file that exists and for repairing a broken one. It
is not how profiles are created. Take a copy of the member before you touch
it. If you have to edit it, the rules of section 1 apply, and one more:
**a line with an id needs a line without an id for the same entity, also for
`*` itself.** If it is missing, for example `JESSPOOL*  ADMIN UPDATE` without
`JESSPOOL*  NONE`, `S RAKFPROF` reports

```
RAKFPROF4  PERMIT RECORD WITHOUT UNIVERSAL ACCESS RECORD
RAKFPROF4  **  PROGRAM TERMINATED  **
IEF450I RAKFPROF RAKFPROF - ABEND S000 U0400
```

and `RAKFPROF8` before it names the offending record. A duplicate line aborts
the reload with `U0300` (`RAKFPROF3`), and a line without an access with `U0500`.

An aborted reload leaves the old chain in effect; the broken file stays where it
is, and **on RAKF 2.0.0 it prevents JES2 from starting at the next IPL.** Fix it
before the next IPL: write the correct file and run `S RAKFPROF`;
`RAKFPROF7  RAKF PROFILES UPDATED` confirms it. Always look at the console
after `S RAKFPROF`.

### What else to know

- **Empty class:** no JESSPOOL line means RACF answers "no profile" (RC 4):
  everyone sees only their own jobs.
- **Class not defined:** if the class is missing from the class table of the
  running RAKF, RAKF writes `RAKF0012` to the console for every check (a client
  can flood the console with it) and foreign jobs are hidden for everybody; own
  jobs stay visible.
- **Missing or invalid RAKF tables.** On RAKF 2.0.0 JES2 does not start without
  valid tables (measured with a broken `PROFILES` and a broken `USERS` at a cold
  start); have valid tables **before** the IPL. The older RAKF of the MVS/CE
  container images allows everything in that case.
- **Withdrawing by group membership** takes effect only after the person has
  logged in again. For an immediate withdrawal change the profile (a user
  permit with `NONE`).
- The loader holds 2000 records without a range check; every owner profile costs
  several lines (example F: three per block).
- Job fields (job name, owner, class, step names) are sanitized before FTPD
  writes them to the control or data connection or to the log.
- Submitting: FTPD adds `USER=` and `PASSWORD=` only to the first JOB card of a
  `STOR` stream. Every further JOB card in the same stream runs as a job of its
  own, without them, under the default account. Such a job has no owner, so it
  falls under the `*` profile (section 7), and the `250` reply names its job id.
  The default account of a batch job is `PROD` / `PRDGROUP` (section 5), without
  Operations authority. The job therefore has no higher rights than the user who
  sent it, as long as `PRDGROUP` gets no more in `SYS1.SECURE.CNTL(PROFILES)`
  than the universal lines give everybody. If you give `PRDGROUP` broad rights,
  you give them to every user who can store a job in the JES view.

### Check it

1. Log in as an ordinary user: `LIST` shows only your own jobs; `SITE
   JESOWNER=*` answers `501`.
2. Log in as a member of the group that the profile permits: `LIST` shows the
   foreign jobs; `RETR` reads them; `DELE` answers `550 Access denied - cannot
   cancel <jobid>` with READ and `250 Cancel successful` with UPDATE.
3. After every `S RAKFPROF` read the console.

---

## 8. Troubleshooting

| Symptom | Cause |
|---------|-------|
| `RAKFUIDS2  INPUT DATA INVALID OR OUT OF SEQ.` | A malformed line, or the table is not in ascending sort order. The utility terminates and the in-core table keeps its **previous** contents — which looks exactly like "my change had no effect". The echoed card masks the password as `********`, so it cannot tell you whether that was the problem |
| Reload rejected and the echoed card looks fine | Most likely a blank password. RAKF 1.2.x requires columns 19–26 to be filled — section 2 |
| `RAKF008W illegal operation -- access denied` | The `RAKFADM` FACILITY profile denies the table-update utility |
| `FTPD004I STC RUNS AS STC/STCGROUP` | No mapping of the STC name `FTPD` applies — the RAKF release has none, or the name is not in its configuration (on RAKF 2.0.0: no userid `FTPD` in `USERS`). FTPD is running with Operations authority |
| `FTPD004W NO SECURITY ENVIRONMENT` | The address space has no ACEE. RAKF permits any access without one |
| `FTPD003W APF SETUP FAILED RC=n` | No APF authorisation and no SVC 244. Nobody can log in: every `PASS` answers `530 Login not possible: FTPD is not APF authorized`. On RAKF 2.0.0 the group of the `FTPD` user may lack READ on `FACILITY` `SVC244` — section 2 |
| `ADDUSER`, `PERMIT` and the other RAKF commands refuse to run | The user lacks the SPECIAL authority (column 30 of `USERS`, RAKF 2.0.0) — section 1 |
| `530 Login incorrect` for a valid user | Wrong password, or the user is not in `USERS`. A user added without `S RAKF` does not exist yet |
| `530 Not authorized for FTP access` | `FTPAUTH` denies this user — check the group in columns 53–60 |
| `550 Access denied to <dsn>` | A `DATASET` profile refuses the access under the **client's** identity, not FTPD's |
| Everyone can reach everything | No `DATASET` profiles. Undefined resources get ALTER — section 4 |
| `S RAKFPROF` ends with `ABEND S000 U0400` | A profile line with an id has no line without an id for the same entity — section 7. The old tables stay in effect |
| `S RAKFPROF` ends with `ABEND S000 U0300` or `U0500` | A second line for the same resource and group (`RAKFPROF3  I/P RECORDS OUT OF ORDER`), or a line without a permission (`RAKFPROF6  ENTITY RECORD WITH NO ACCESS AUTHORITY`). The `RAKFPROF8` line before the message is the record that stopped the load. Section 7, "Writing `PROFILES` by hand" |
| `550 Jobid … not found` for a job that exists | The job is hidden: it belongs to another owner and no JESSPOOL profile gives you READ — section 7 |
| `501` for `SITE JESOWNER=…` | A pattern, a value of more than 8 characters, or no READ on the owner's jobs — section 7 |

---

## Related

- [installation.md](installation.md)
  — installing FTPD itself
- [FTPD_CONCEPT.md](../internals/FTPD_CONCEPT.md)
  — architecture, including the session/identity model
