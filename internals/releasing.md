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

Current: 1.3.0 (`TFTP130`, deleting `TFTP120`) and the patch releases 1.3.1
(`TFTP131`, deleting `TFTP130` only) and 1.3.2 (`TFTP132`, deleting `TFTP131`
only) are released. 1.3.1 and 1.3.2 were cut on the branch `release-1.3.x`;
mbt derives the id from the minor version only, so `mbt.toml` carried an
explicit `fmid` and `delete` in `[smp]` on that branch. From 1.3.3 on, releases
are cut from `main`: the branch `release-1.3.x` is contained in `main` and is
not used any more, and the tags `v1.3.0` to `v1.3.2` hold its history.
Not yet checked on any stand: `TFTP132`, `TFTP133` and `TFTP140`.
Because 1.3.2's `delete` named `TFTP131` only, a system that skipped 1.3.1
had to install 1.3.1 first. 1.3.3 does not have that rule: it names all three
earlier ids (below).

`main` is 1.3.3-dev and carries the explicit `fmid = "TFTP133"` and
`delete = ["TFTP132", "TFTP131", "TFTP130"]` in `[smp]`; 1.3.3 is a patch
release and mbt does not derive its id. The multi-id delete was measured on
2026-10-30 (ftpd#373) on a system carrying only 1.3.0, only 1.3.1 or only 1.3.2:
all three install cleanly, the APPLY ends RC 4 (`HMA2461`) and the ids stay as
tombstones. It does not lift the element-ownership wall: a system on `TFTP120`
or older still installs 1.3.0 first. The released package itself has not been
measured. `TFTP140` is reserved for 1.4.0. A development build of `main`
packaged before this change carries `TFTP140` (`docs/uninstall.md`). When 1.4.0
is cut, `[smp]` needs an explicit `delete` that names `TFTP133` (and `TFTP132`,
`TFTP131` and `TFTP130` for a system that skipped a patch), and the explicit
`fmid` goes: the derived `TFTP140` is then right. A development
build of `main` now carries `TFTP133`, the id of the release: do not install it
on a system that will take 1.3.3 (the rule below about test installs).

`TFTP111` was assigned to a 1.1.1 that was never cut (the libc370 2.0 port made
the next release 1.2.0) and is unspent and unassigned. Burned: `TFTP100`
(1.0.0–1.0.2), `TFTP110` (1.1.0, released 2026-09-14), `TFTP120` (1.2.0,
released 2026-10-13), `TFTP130` (1.3.0, released 2026-10-22), `TFTP131` (1.3.1,
released 2026-10-27) and `TFTP132` (1.3.2, released 2026-10-28); `TFTP120` and
`TFTP130` were free on the maintainer's word, no `LIST` was run on any stand
before they shipped. `docs/uninstall.md` names every spent id and carries
`TFTP132`; add each later id there.

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

The release workflow (`.github/workflows/release.yml`) has two jobs. The first
is mbt's `release3.yml`: it builds the SMP package and publishes the release
page at once (not as a draft), and it deletes an existing release of the same
tag first, with every asset on it. The second job, `books`, runs after it
(`needs: release`): in a clean checkout of the tagged commit with the
`bookmaster` submodule, it builds both PDFs with the pinned typst
(`.github/actions/install-typst`, the same pin as the `books` CI job), takes the
SMP files from the release as published, writes `SHA256SUMS` over those and the
PDFs, uploads PDFs and `SHA256SUMS`, and ends with
`tools/check-release-assets.sh --assets-only`, so a release that lacks any of
them makes the run fail. That mode does not compare the page text: while the
job runs, the page still has the generated notes of `release3.yml`, which name
no asset. The first real run of the job is the next release; whoever cuts it
watches that run to the end. The job needs no `SOURCE_DATE_EPOCH`: the Makefile fixes the PDF
creation date at 0 (below).

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
   removed or kept on purpose. The heading line of the notice ("Draft Edition"
   or "First Edition") says the same as the edition digit (`-0` Draft, `-1`
   First Edition); a change to the digit changes the heading and renames the
   PDFs in `docs/books/Makefile`.
4. `docs/installation.md` and `docs/uninstall.md` agree with the books on the
   libc370 level and the FMID. `docs/uninstall.md` holds the FMID table, the
   `DELETE` list of the dummy function `TFTPDEL` and the `UCLIN` jobs: add the
   new id to the `DELETE` list, to both `UCLIN` jobs and to their `LIST`
   statements. When a new id makes the `++VER(Z038) DELETE(...)` control card exceed column 71, wrap the operand after a comma and continue it on the next card line (as in the development-build example in `docs/uninstall.md`); keep every line within column 71 in both `docs/uninstall.md` and `ug-remove.typ`. On the release candidate, run the removal job and confirm `RECEIVE` completes with RC 04, not RC 08, before proceeding to `UCLIN`. The book chapters `ug-install` and `ug-remove` repeat the same
   jobs: change them in the same commit, together with the FMID table, the
   file names (`ftpd-X.Y.Z-*`) and the captures under `docs/books/ex/ug-install/`
   and `docs/books/ex/ug-remove/`. The jobs and captures were last measured on
   the package of 1.3.3 built before the release (the edition notice says so); run them once
   on a throwaway container with the release candidate installed from the SMP
   package, replace the captures with that output, compare it with the text,
   and name the release they were measured with in the notice. Where a screen
   was not measured again, the text must say which release it comes from. The
   1.3.3 run did not cover the LIST and NLST sessions
   (`docs/books/ex/ug-data/`), the fallback `UCLIN` job, the development-build
   section of `docs/uninstall.md` and `ug-remove`, the `RESTORE`/`REJECT`
   refusal, or the two MVS/CE screens `ce-jclerr.txt` and `ce-rerun.txt` of
   `ug-install` (job skipped, then rerun); they are still from 1.3.2 or earlier, and so are the "measured on
   1.3.2" notes next to them. Re-run them when a release changes what they
   show. Do the same for the inline captures of `docs/uninstall.md` and
   `docs/installation.md`.

After the tag, the `books` job has attached the books. Wait for the whole
run to be green; a red `books` job means the release is incomplete, whatever
the page says. If the job failed for a reason outside the repository, re-run
it (it is safe to run again). Only when it cannot run, attach the files by
hand, from the tagged commit:

```sh
make -C docs/books
mkdir out
gh release download vX.Y.Z -R mvslovers/ftpd -p 'ftpd-*' -D out
cp docs/books/ml06-000[12]-*.pdf out/
(cd out && sha256sum * > ../SHA256SUMS)
gh release upload vX.Y.Z out/ml06-000[12]-*.pdf SHA256SUMS -R mvslovers/ftpd
```

A manual build is from the tagged commit, in a clean checkout with the
submodule initialised at the pinned commit. No environment variable is needed:
the Makefile passes `--creation-timestamp 0` to `typst compile`, so the PDF does
not carry the build time (`CreationDate`, `ModDate`, the XMP dates). The flag
takes precedence over `SOURCE_DATE_EPOCH`, so that variable has no effect on
these builds, and the date stamp in the PDF is 1970-01-01 by design. Two plain
`make -C docs/books` runs a few seconds apart gave byte-identical PDFs
(`sha256sum` equal) with typst 0.15.1. That holds for the same typst version
and the pinned bookmaster; it was not measured on another version or platform.
`SHA256SUMS` always describes the uploaded files. The PDFs attached to v1.3.0
and v1.3.1 carry their build time and cannot be reproduced bit for bit; a
rebuild of them has the same size and the same text, only the date stamps and
the instance id differ. The PDFs attached to v1.3.2 were built with a fixed
timestamp of 0, as this Makefile does now. The upload by the `books`
job is part of the release the maintainer approved with the tag. A manual
upload is a publication of its own: it needs the maintainer's go-ahead naming
the release and the files.

## Before the release page is published

The release page is live as soon as the tag has been pushed, with the SMP
package; the `books` job adds the books and `SHA256SUMS` a few minutes later.
The page text that the maintainer approved is put on it after the whole run is
green. Before that, and again afterwards, compare it with what the release
carries:

```sh
tools/check-release-assets.sh vX.Y.Z approved-page.md   # the approved text, before it is put on the page
tools/check-release-assets.sh vX.Y.Z                    # the page as published
```

`--assets-only` (before the version) skips the comparison with the text; the
`books` job uses it.

The script (read-only; needs `gh`) compares `gh release view --json assets`
with the file names in the page's asset tables: every name in the text must be
an asset, and every asset must be named in the text. It also requires the
three SMP files, one PDF of each book and `SHA256SUMS`, that `SHA256SUMS` lists
exactly the other assets, and that every sum matches the file as downloaded.
Any deviation is a stop: do not publish the page. Attach what is missing, or
send the release back (below); do not cut the page to make the check pass.

**A page the maintainer has approved is not shortened.** If something it
names cannot be delivered (a book that does not build, an asset that is not
there), the release goes back to the maintainer with what is missing. The
page is neither cut nor published as it is. v1.3.3 was published without its
books and `SHA256SUMS`, and nothing compared the page and the assets.
