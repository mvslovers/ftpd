# Changelog

Changes that matter to someone who installs or runs FTPD. The notes for earlier
releases are on their [release pages](https://github.com/mvslovers/ftpd/releases).

## 1.3.2 — unreleased

A patch release for 1.3.x: fixes only, no new function. It is a drop-in
replacement for 1.3.1. SMP FMID `TFTP132`, which deletes `TFTP131`.

### Fixed

- **A parenthesis in a data set name is checked to the end of the name.** The
  check stopped reading at the first `(`, so what followed was never looked at
  in `CWD`, in the arguments of `LIST` and `NLST`, or in the member of `DELE`,
  `RNFR`, `RNTO` and the transfer commands. A member now has to be exactly
  `(NAME)` at the end of the name, spelled with the characters a qualifier may
  hold; anything else is refused with `501`. `LIST` and `NLST` accept a member
  only in the form `DSN(pattern)`. (#335)
- **`CWD`, `MKD` and `RMD` refuse a name with a member.** These commands act on
  a data set as a whole. `RMD 'DSN(MEMBER)'` used to ask the security product
  about a name no profile can cover and then delete the member, answering
  `250`. All three now answer `501` before the security product, the catalog or
  IDCAMS are asked, so the access check only ever sees the data set name. (#339)
- **IDCAMS commands no longer depend on the name.** The command text is passed
  to the IDCAMS call as an argument, not as the format string, so a `%` in a
  name cannot be read as a format directive. (#339)

### Upgrade

Install it like any other release: the install job receives, applies and
accepts `TFTP132`, and the `DELETE` operand removes `TFTP131` and its load
module. A system that still carries `TFTP130` (it skipped 1.3.1) installs 1.3.1
first. The data set names carry no version and the configuration is unchanged,
so the procedure and the parameter member stay as they are. Stop the started
task before the install replaces the load library.
