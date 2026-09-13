# Removing FTPD

This is the supported way to take FTPD back off an MVS 3.8j system and free
its FMID for a re-install.

> **The removal instructions inside the shipped release archive are wrong.**
> `README.md` in `ftpd-<version>-dist.zip` says to run `RESTORE` and then
> `REJECT`. Both are refused once the FMID has been accepted — which the
> install job does, in the same run as the APPLY. Use this document instead.

## What this release put on the system

| | |
|---|---|
| FMID | `TFTP110` (1.1.x) or `TFTP100` (1.0.x) |
| Load module | `FTPD` |
| Target library | `FTPD.LINKLIB` |
| Distribution library | `FTPD.AFTPDLOD` |
| Sample library | `FTPD.SAMPLIB` |

**Substitute the FMID of the release you are removing** in every job below.
One FMID names a whole minor level: `TFTP100` is all of 1.0.x, `TFTP110` all of
1.1.x. `LIST CDS SYSMOD(...)` tells you which one a system carries.

The data set names are the same in every release. Before 1.1.0 they carried the
patch level (`FTPD.V1R0M2.LINKLIB`) while the FMID moved only per minor, so two
releases could sit side by side and it was possible -- easy, in fact -- to
scratch the libraries of one while the other stayed installed, with SMP
reporting success throughout. That is gone: there is one installation, and the
names below are it. Which is also why they are worth reading twice.

The staging library `FTPD.FTPDLOAD` is not listed because the install job's
`CLEANUP` step already scratched it.

---

## 1. Stop the server

```
/P FTPD
```

## 2. Cut the FMID out of the SMP inventory

Submit this. It edits the CDS and the ACDS and touches no library:

```
//FTPDUCL  JOB (SYS),'FTPD UNINSTALL',
//             CLASS=A,MSGCLASS=H,MSGLEVEL=(1,1),
//             REGION=4096K
//UCLIN   EXEC SMPAPP
//SMPCNTL  DD  *
 UCLIN CDS .
  DEL SYSMOD(TFTP110) MOD(FTPD) .
  DEL MOD(FTPD) .
  DEL LMOD(FTPD) .
  DEL SYSMOD(TFTP110) .
 ENDUCL .
 UCLIN ACDS .
  DEL SYSMOD(TFTP110) MOD(FTPD) .
  DEL MOD(FTPD) .
  DEL SYSMOD(TFTP110) .
 ENDUCL .
/*
//LIST    EXEC SMPAPP
//SMPCNTL  DD  *
 RESETRC .
 LIST CDS  SYSMOD(TFTP110) .
 LIST ACDS SYSMOD(TFTP110) .
/*
//
```

Every `DEL` reports `HMA2550 UPDATE COMPLETE`, and each `UCLIN` block ends
`RC 00`.

## 3. Read the LIST — this is the actual result

The `LIST` step is what tells you whether it worked. Both zones must answer:

```
THE FOLLOWING SELECTED ENTRIES WERE NOT FOUND OR WERE NOT ELIGIBLE
FOR PROCESSING
 TYPE        NAME
 SYSMOD      TFTP110
```

with `HIGHEST RETURN CODE IS 04`. **RC 04 and an empty list means the FMID is
free.** Both zones matter: the CDS records what is applied, the ACDS what is
accepted, and they are separate inventories — an id gone from one and present
in the other is not free.

## 4. Scratch the libraries

`UCLIN` edits the inventory only. The load module is still in the target
library and SMP's accepted copy is still in the distribution library, so a
re-install would find both datasets already there and its allocation job would
fail:

```
  DELETE FTPD.LINKLIB  NONVSAM SCRATCH PURGE
  DELETE FTPD.AFTPDLOD NONVSAM SCRATCH PURGE
```

Leave `FTPD.SAMPLIB` alone if you like — the install job's `DELOLD` step
scratches it on its own.

**These are the live libraries, not a previous release's.** Run this only when
you mean to remove FTPD, or as the middle of an upgrade: stop the server, cut
the FMID (step 2), scratch these, then run the new release's allocation and
install jobs. Skipping the scratch makes the allocation job fail, because it
allocates `DISP=(NEW,CATLG,DELETE)` and the data sets are already there.

## 5. What is not removed, because SMP never owned it

- The procedure and the configuration member you copied into your PROCLIB and
  PARMLIB.
- Your RAKF definitions — the `FTPD` user and the `FTPAUTH` profile. See
  [FTPD_RAKF_SETUP.md](https://github.com/mvslovers/ftpd/blob/main/doc/FTPD_RAKF_SETUP.md).

Those are yours to delete.

---

## Why `RESTORE` and `REJECT` do not work

Worth knowing, because the messages point away from the cause.

**`RESTORE` refuses an accepted SYSMOD.**

```
HMA2452 ** SYSMOD <fmid> SELECTED FOR RESTORE HAS BEEN ACCEPTED
HMA3703 ** RESTORE PROCESSING TERMINATED BECAUSE FUNCTION SYSMOD
           <fmid> FAILED
HMA2050    RESTORE PROCESSING COMPLETED - HIGHEST RETURN CODE IS 12
```

**`REJECT` then fails for an unrelated-looking reason.**

```
HMA2462 ** SYSMOD <fmid> NOT FOUND ON SMPPTS LIBRARY
HMA2260    REJECT PROCESSING TERMINATED FOR SYSMOD <fmid>
HMA2050    REJECT PROCESSING COMPLETED - HIGHEST RETURN CODE IS 12
```

The `ACCEPT` removes the modification control statements from `SYS1.SMPPTS`,
and `REJECT` works from that member. So accepting a function SYSMOD closes
both documented routes at once: `RESTORE` because it was accepted, `REJECT`
because accepting took away what it needs. `UCLIN` is not a workaround here,
it is the only way.

This was measured on 2026-08-14 against an accepted FMID installed from a
package built by this generator, on an MVS/CE system running SMP 4 level
04.48.

## Why the install job accepts at all

The `ACCEPT` fills the distribution library, which is the base a later
`RESTORE` of a **PTF** returns to. Without it, a `RESTORE` would delete the
module rather than revert it, because there would be no previous level to go
back to. The cost is what this document is about: the FMID itself becomes
permanent by documented means.
