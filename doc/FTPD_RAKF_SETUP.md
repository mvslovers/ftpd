# FTPD RAKF Configuration Guide

FTPD requires RAKF for authentication and dataset access control.
This guide covers the setup for FTPD and is reusable for other
mvslovers STCs (HTTPD, UFSD).

---

## 1. Overview

FTPD uses two levels of RAKF security:

1. **Login authentication** — USER/PASS verified via RACINIT (SVC 244)
2. **Dataset access control** — RACHECK against DATASET class profiles
   before every RETR, STOR, DELE, MKD, RMD, and RNFR/RNTO operation

Additionally, a FACILITY class profile `FTPAUTH` controls which users
are allowed to use FTP at all.

The FTPD STC itself runs under its own identity (`FTPD` in group `USER`),
established via RACINIT at startup (see §5).

---

## 2. RAKF User and Group Setup

### 2.1 Create the STC User

Add the FTPD user to `SYS1.SECURE.CNTL(USERS)`:

```
FTPD     USER  DEFAULT-GROUP(USER)
```

This user is used as the STC identity.  It does not need a password
because RACINIT uses `PASSCHK=NO` for the STC identity switch.

### 2.2 Verify the USER Group Exists

The `USER` group should already exist on most MVS/CE and TK4- systems.
If not, add it to `SYS1.SECURE.CNTL(USERS)`:

```
USER     GROUP
```

### 2.3 Alternative: RAKFCL Commands

Instead of editing `SYS1.SECURE.CNTL` directly, you can use RAKFCL:

```
RX 'SYS2.CMDPROC(RAKFCL)' 'ADDUSER FTPD DFLTGRP(USER)'
```

After adding users or groups, reload the RAKF tables:

```
/F RAKF,RELOAD
```

---

## 3. FACILITY Class Profile: FTPAUTH

The `FTPAUTH` resource in the `FACILITY` class controls which users
may log in via FTP.  FTPD checks this after successful password
verification.

### 3.1 Define the Profile

Add to `SYS1.SECURE.CNTL(PROFILES)`:

```
FTPAUTH  FACILITY  UACC(NONE)
```

### 3.2 Grant Access

Grant READ access to groups or users that should be allowed FTP access:

```
FTPAUTH  FACILITY  UACC(NONE)  ID(USER READ)
```

This allows all users in the `USER` group to log in via FTP.
For finer control, create an `FTPUSER` group and grant access only
to that group:

```
FTPAUTH  FACILITY  UACC(NONE)  ID(FTPUSER READ)
```

### 3.3 Alternative: RAKFCL Commands

```
RX 'SYS2.CMDPROC(RAKFCL)' 'RDEFINE FACILITY FTPAUTH UACC(NONE)'
RX 'SYS2.CMDPROC(RAKFCL)' 'PERMIT FTPAUTH CLASS(FACILITY) ID(USER) ACCESS(READ)'
/F RAKF,RELOAD
```

### 3.4 Open Access (No FTPAUTH Profile)

If the `FTPAUTH` profile is **not defined** in RAKF, access is allowed
by default (RAKF returns "UNDEFINED RESOURCE - ACCESS ALLOWED").
This is acceptable for development but should be locked down in
production.

---

## 4. Dataset Access Control

FTPD performs RACHECK against the `DATASET` class before every
dataset I/O operation.  The access levels are:

| FTP Command | RACHECK Attribute | Meaning |
|-------------|-------------------|---------|
| RETR (GET)  | READ              | Download dataset |
| STOR (PUT)  | UPDATE            | Upload / create dataset |
| DELE        | ALTER             | Delete dataset or PDS member |
| MKD         | ALTER             | Create new PDS |
| RMD         | ALTER             | Remove PDS |
| RNFR / RNTO | ALTER            | Rename dataset |

Dataset profiles are defined in `SYS1.SECURE.CNTL(PROFILES)`.
For example, to allow `USER` group READ access to all `SYS1.**`
datasets:

```
SYS1     DATASET  UACC(NONE)  ID(USER READ)
```

If no matching DATASET profile exists, RAKF allows access by default
("UNDEFINED RESOURCE - ACCESS ALLOWED").

### 4.1 Per-User HLQ Access

A common pattern is to give each user ALTER access to their own HLQ:

```
IBMUSER  DATASET  UACC(NONE)  ID(IBMUSER ALTER)
MIKEG1   DATASET  UACC(NONE)  ID(MIKEG1 ALTER)
```

---

## 5. STC Identity Switch

At startup, FTPD performs an inline RACINIT to switch from the
default STC identity (PROD/PRDGROUP) to `FTPD/USER`.  This requires:

1. The FTPD load module is link-edited with `AC(1)` (APF authorized)
2. The FTPD STEPLIB is APF authorized
3. The `FTPD` user exists in RAKF (see §2)

On success, the console shows:

```
FTPD004I STC identity set to FTPD/USER via RACINIT
```

If the RACINIT fails (e.g., user not defined), the STC continues
under the default identity with a warning:

```
FTPD004W RACINIT ENVIR=CREATE failed RC=nn
```

---

## 6. Reuse for Other STCs

The same pattern applies to HTTPD and UFSD:

| STC  | User  | Group | FACILITY Resource |
|------|-------|-------|-------------------|
| FTPD | FTPD  | USER  | FTPAUTH           |
| HTTPD| HTTPD | USER  | (none currently)  |
| UFSD | UFSD  | USER  | (none currently)  |

Each STC needs its own user in `SYS1.SECURE.CNTL(USERS)`.
The `USER` group is shared.

---

## 7. Quick Setup Checklist

1. Add `FTPD` user to `SYS1.SECURE.CNTL(USERS)` or via RAKFCL
2. Verify `USER` group exists
3. (Optional) Define `FTPAUTH` FACILITY profile
4. (Optional) Define DATASET profiles for access control
5. Reload RAKF: `/F RAKF,RELOAD`
6. Start FTPD: `/S FTPD`
7. Verify: `FTPD004I STC identity set to FTPD/USER via RACINIT`
8. Test login: `ftp <host> 2121` with a valid RAKF user

---

## 8. Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| `RAKF0010I STC FTPD STARTED USING DEFAULT STC ACCOUNT` | FTPD user not defined in RAKF | Add FTPD user, `/F RAKF,RELOAD` |
| `FTPD004W RACINIT ENVIR=CREATE failed` | FTPD user not defined or APF issue | Check RAKF users, verify APF auth |
| `530 Login incorrect` | Wrong password or user not in RAKF | Verify user exists, password is correct |
| `530 Not authorized for FTP access` | FTPAUTH profile denies access | Grant READ to user's group |
| `550 Access denied to <dsn>` | DATASET profile denies access | Check DATASET profiles in RAKF |
| `RAKF0008 UNDEFINED RESOURCE - ACCESS ALLOWED` | No profile defined | Expected in dev; define profiles for production |
