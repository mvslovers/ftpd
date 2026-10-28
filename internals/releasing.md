# Releasing FTPD

Moved here from `CLAUDE.md`. The ecosystem-wide rules (id format, burned
ids, `DELETE`, the element-ownership wall, `LIST`) are in
`~/repos/mvs/.workspace/context/smp.md`; this file holds what is particular
to ftpd. Read both before cutting a release.

## The SMP4 FMID

The id is the release: `T` + three product letters + the three version digits.
One id per release, **spent exactly once**, and each release's SYSMOD deletes
its predecessor:

```toml
[distribution.smp]
fmid   = "TFTP120"
delete = ["TFTP110"]
```

**No version component may ever exceed 9** — a 7-character id has no room for
a second digit. At patch 9 cut the next minor, at minor 9 the next major;
ftpd 1.2.10 cannot be expressed and must not be released.

Current: 1.3.0 is released (`TFTP130`, deleting `TFTP120`). The patch release
1.3.1 on the branch `release-1.3.x` is **`TFTP131`**, deleting `TFTP130` only; mbt
derives the id from the minor version only, so `mbt.toml` needs an explicit
`fmid = "TFTP131"` and `delete = ["TFTP130"]` in `[smp]` when it is cut (see
the comment there). Not yet checked on any stand. `TFTP111` was assigned to a 1.1.1 that
was never cut (the libc370 2.0 port made the next release 1.2.0) and is unspent
and unassigned. Burned: `TFTP100` (1.0.0–1.0.2), `TFTP110` (1.1.0, released
2026-09-14), `TFTP120` (1.2.0, released 2026-10-13; free on the maintainer's
word, no `LIST` was run on any stand before it shipped) and `TFTP130` (1.3.0).
`docs/uninstall.md` names every spent id and carries `TFTP131`; add each later
id there.

Never re-spend an id, and never install a test package under the real one: a
test needs a throwaway id **and** throwaway module names, because SMP keys
element ownership on `MOD(name)`, not on the target library. See the root
`CLAUDE.md` for the full rule and the measurements behind it.

## Before every libc370 version raise

Before **every** raise of the libc370 version FTPD is built against, run
Test 18 case 1 (`test/test_ftpd.sh`) on MVS with an empty JESSPOOL class. An
RC 0 there is a blocker and is not fixed by adjusting the table.

Why: the JES spool authorization (#90) takes the SAF return code of
`racf_auth()` as it comes and reads RC 4 ("no profile") as "foreign jobs are
hidden". Only libc370 2.25.1 was measured. If a libc370 returned 0 for a class
without profiles, every job on the spool would be open to every user, and if
it returned 8, every foreign job would be shut; neither is a thing for the
table in `ftpd_jwn_level` to cover up.

Run it on a throwaway container (RAKF 2.0.0, see the header of Test 18 for the
variables) with `FTPD_GATE=1`, `FTPD_IMAGE` (image and digest) and, without a
console, `FTPD_LIBC370`. The output names the libc370 version, the RAKF level
(detected, not assumed) and the image with its digest, so the run belongs to a
version. In gate mode a RAKF level other than 2.0.0, an unknown image or
libc370 version, and every FINDING make the run fail; cases that were SKIPPED
go into the release notes.

## The books

ML06-0001 (User's Guide) and ML06-0002 (Reference) live in `docs/books/` and
are built with typst and the `bookmaster` submodule. The document numbers are
registered in the README of `mvslovers/bookmaster`. A release attaches both
PDFs to its release page with their `SHA256SUMS` lines (the rule agreed for
all products in `~/repos/mvs/.workspace/context/books.md`).

The release workflow (`.github/workflows/release.yml`, mbt's `release3.yml`)
builds and publishes the SMP package only. It does not build or attach the
books; that is a manual step after the tag exists.

Before the tag:

1. The books' part of the release is ready: every behaviour change in the
   release has its section, and the table "What Is New in FTPD X.Y.Z" of that
   release has one row per change. Check it against the closed issues and merged PRs of the
   milestone, and against the source, not against the PR texts.
2. `make -C docs/books` builds both PDFs without errors and without warnings
   beyond the two known ones from the template (the experimental bundle
   export and the outline that does not converge). The `books` CI job builds
   with the pinned typst; use the same version.
3. The edition notice in `docs/books/ml06-0001.typ` and
   `docs/books/ml06-0002.typ` names the release, and the "Draft" paragraph is
   removed or kept on purpose. A change to the edition digit (`-0` Draft,
   `-1` First Edition) also renames the PDFs in `docs/books/Makefile`.
4. `docs/installation.md` and `docs/uninstall.md` agree with the books on the
   libc370 level and the FMID. `docs/uninstall.md` holds the FMID table, the
   `DELETE` list of the dummy function `TFTPDEL` and the `UCLIN` jobs: add the
   new id to the `DELETE` list, to both `UCLIN` jobs and to their `LIST`
   statements. The book chapters `ug-install` and `ug-remove` repeat the same
   jobs: change them in the same commit, together with the FMID table, the
   file names (`ftpd-X.Y.Z-*`) and the captures under `docs/books/ex/ug-install/`
   and `docs/books/ex/ug-remove/`. Run the jobs once on a throwaway container
   with the release candidate installed from the SMP package, replace the
   captures with that output, and compare it with the text. The captures of
   a LIST or NLST session (`docs/books/ex/ug-data/`) are re-run against the
   release candidate in the same way. Do the same for the inline captures of
   `docs/uninstall.md` and `docs/installation.md`.

After the tag, with the release page created by the workflow:

```sh
make -C docs/books
sha256sum docs/books/ml06-0001-*.pdf docs/books/ml06-0002-*.pdf > SHA256SUMS
gh release upload vX.Y.Z docs/books/ml06-000[12]-*.pdf SHA256SUMS -R mvslovers/ftpd
```

Build from the tagged commit, in a clean checkout with the submodule
initialised at the pinned commit, so the PDFs can be rebuilt byte-for-byte
from the tag. Uploading to a release is a publication: it needs the
maintainer's go-ahead naming the release and the files, like the release
itself.
