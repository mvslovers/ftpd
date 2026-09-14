# Removing FTPD

This is the supported way to take FTPD back off an MVS 3.8j system and free
its FMID for a re-install.

> **The removal instructions inside a 1.0.x release archive are wrong.**
> `README.md` in `ftpd-1.0.?-dist.zip` says to run `RESTORE` and then
> `REJECT`. Both are refused once the FMID has been accepted — which the
> install job does, in the same run as the APPLY. Use this document instead.
> From 1.1.0 the shipped README says so itself and points here.

## What this release put on the system

| | |
|---|---|
| FMID | `TFTP110` (1.1.x) or `TFTP100` (1.0.x) |
| Load module | `FTPD` |
| Target library | `FTPD.LINKLIB` |
| Distribution library | `FTPD.AFTPDLOD` |
| Sample library | `FTPD.SAMPLIB` |

**Substitute the FMID of the release you are removing** in every job below.
Since 1.1.0 there is one FMID per *release*: `TFTP110` is 1.1.0 and nothing
else, `TFTP111` will be 1.1.1. `TFTP100` is the exception, from the policy that
came before it -- it covered 1.0.0, 1.0.1 and 1.0.2 alike. `LIST CDS
SYSMOD(...)` tells you which one a system carries.

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
  DEL SYSMOD(TFTP100) .
 ENDUCL .
 UCLIN ACDS .
  DEL SYSMOD(TFTP110) MOD(FTPD) .
  DEL MOD(FTPD) .
  DEL SYSMOD(TFTP110) .
  DEL SYSMOD(TFTP100) .
 ENDUCL .
/*
//LIST    EXEC SMPAPP
//SMPCNTL  DD  *
 RESETRC .
 LIST CDS  SYSMOD(TFTP110) .
 LIST ACDS SYSMOD(TFTP110) .
 LIST CDS  SYSMOD(TFTP100) .
 LIST ACDS SYSMOD(TFTP100) .
/*
//
```

Every `DEL` reports `HMA2550 UPDATE COMPLETE`, and each `UCLIN` block ends
`RC 00`.

### Why `TFTP100` is in a job that removes `TFTP110`

Because installing 1.1.0 left an entry for it. The 1.1.0 SYSMOD carries
`++VER(Z038) DELETE(TFTP100)`, and SMP records that deletion in both zones as
a tombstone -- even on a system that never ran 1.0.x:

```
TFTP100   TYPE  = FUNCTION
          DELBY = TFTP110
```

Removing `TFTP110` without removing that leaves the tombstone pointing at a
SYSMOD which is no longer there. It is harmless in itself, but it is also the
thing that makes `LIST` ambiguous afterwards -- see the next section. A plain
`DEL SYSMOD(TFTP100)` clears it; measured on mvsdev 2026-09-14 with throwaway
ids (`TTMPCLN JOB00311`), where it took the tombstone back to `NOT FOUND`
alongside the SYSMOD that created it.

Substitute the predecessor of whatever you are removing: for a future
`TFTP111` that is `TFTP110`.

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

**There is a third answer, and the return code alone does not distinguish it.**
An id that some release deleted comes back at `RC 00` with a stanza holding
nothing but a `DELBY`:

```
TFTP100   TYPE  = FUNCTION
          DELBY = TFTP110
```

That is a tombstone, not an installation: no `STATUS`, no `FMID`, no elements.
Read the stanza rather than the return code — `RC 00` here does not mean
something is installed, and it is what you will see for `TFTP100` on any system
that installed 1.1.0, including one that never ran 1.0.x. The `DEL
SYSMOD(TFTP100)` above is what clears it.

## 4. Scratch the libraries

`UCLIN` edits the inventory only. The load module is still in the target
library and SMP's accepted copy is still in the distribution library, and a
re-install does not clear them out for you:

```
  DELETE FTPD.LINKLIB  NONVSAM SCRATCH PURGE
  DELETE FTPD.AFTPDLOD NONVSAM SCRATCH PURGE
```

Leave `FTPD.SAMPLIB` alone if you like — the install job's `DELOLD` step
scratches it on its own.

**These are the live libraries, not a previous release's.** Run this only when
you mean to remove FTPD.

**An upgrade does not come through here any more.** It used to: before 1.1.0 a
new FMID could not take ownership of an element the old one held, so the only
way forward was to cut the old id out with step 2 first. Since 1.1.0 each
release's SYSMOD carries `++VER DELETE(<predecessor>)` and SMP moves the
ownership itself. Install the new release over the old one and follow the
upgrade section of its `README.md`; the only thing an upgrade still borrows
from this document is the `DELETE` statements above, for the *previous*
release's libraries when its data set names differed.

**Do not count on the allocation job to tell you that you skipped this.** It
allocates `DISP=(NEW,CATLG,DELETE)`, so a second run over data sets that are
already there does not fail -- measured on mvsdev, job FTPDALC JOB00273:

```
IEF142I FTPDALC ALLOC - STEP WAS EXECUTED - COND CODE 0000
IEF287I   FTPD.LINKLIB      NOT CATLGD  2
IEF287I   FTPD.AFTPDLOD     NOT CATLGD  2
```

`NOT CATLGD 2` is the catalog refusing a duplicate name, and it is not an
error: the step ends RC 0, and the newly allocated data set stays on whatever
volume `UNIT=SYSDA` picked, uncataloged. In that run it picked the other
volume of the pair, so the system was left with `FTPD.LINKLIB` on both WORK00
and WORK01 -- the cataloged one and an empty twin nothing points at. Read the
`IEF285I`/`IEF287I` lines, not the condition code.

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
