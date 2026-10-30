# Changelog

Changes that matter to someone who installs or runs FTPD. The notes for earlier
releases are on their [release pages](https://github.com/mvslovers/ftpd/releases).

## 1.3.3 — unreleased

A patch release for 1.3.2: fixes only, no new function. It is cut from `main`.
SMP FMID `TFTP133`, which deletes `TFTP132`.

### Fixed

- **`RETR` from UFS no longer answers `226` for a file that was not sent to the
  end.** The server loop ended at the first read that returned no data, and
  that is the result of the end of the file and of a failed read alike, so a
  read error was answered `226 Transfer complete` and a client kept a
  truncated file as a complete one. A send that failed on the data connection
  was answered `226` too. FTPD now asks the file handle whether the read
  failed. A read error is answered
  `451 Read error on the server after N bytes: transfer is incomplete.`, and a
  lost data connection `426 Connection closed; transfer aborted after N bytes.`
  (when both happened, `426`). A transfer that fails is not counted in the
  number of transfers of the session. FTPD writes a line to the log for each:
  `RETR: read failed on PATH after N bytes sent (rc=N)` or
  `RETR: data connection failed on PATH after N bytes`. (#313)

- **A burst of connections is greeted.** The listener takes the waiting
  connections once each second, and it asked the host for a queue of 10 for
  them. A burst of more than about ten clients that connected together lost
  some of them: the client's connect succeeded, and the `220` came seconds late
  or not at all. FTPD now asks for 128. In a test with 30 clients that logged
  in at once (`MAXSESSIONS` 40), 27 of 30 runs lacked at least one greeting
  before, and all 30 runs were complete now, with the last `220` after at
  most 1.1 seconds. Above `MAXSESSIONS`, the rest is still closed at once
  without a reply. The 128 is a request: the host limits the queue with a
  limit of its own, and a limit below 128 was not measured. (#327)

## 1.3.2 — 2026-10-28

A patch release for 1.3.x: fixes only, no new function. It is a drop-in
replacement for 1.3.1. SMP FMID `TFTP132`, which deletes `TFTP131`.

### Fixed

- **`LIST` and `NLST` read the catalog at the level of the name you give.**
  `LIST 'SYS2.PROCLIB'` or `NLST 'SYS2.**'` returned nothing unless the working
  directory was `SYS2`: the catalog was read at the level of the working
  directory and the name only filtered that result, so a data set outside the
  working directory could never be in it. A client that sends absolute names
  without a `CWD` first now gets them listed. The level is the qualifiers in
  front of the first wildcard qualifier, or the parent level for a name without
  a wildcard; a pattern that starts with a wildcard keeps the working
  directory. A level that is not a plain data set name (one with a `(` or a
  quote in it) is not used. (#331)
- **The run time of the data set name match is bounded.** A pattern with
  several `**` between literal qualifiers tried the rest of the pattern at
  every qualifier boundary, and the number of tries was not limited, so such a
  pattern against a name with many qualifiers could keep a session busy for a
  long time (millions of tries for a 22-qualifier name). The match now
  gives up with "no match" after 10000 tries. Results are the same wherever the
  old matcher finished within that limit, and consecutive `**` in a pattern
  count as one. (#337)
- **A listing that shows nothing answers `550`.** `LIST` and `NLST` of a name
  or pattern that matched no data set sent `150`, an empty list and `226`, and
  a client that waits for at least one line hung. The matches are now counted
  before the data connection is opened, and none is answered
  `550 No data sets found.`, for a pattern, a name and an empty level alike.
  (#344)
- **A `**` that is not a qualifier of its own is refused.** `**ASM`,
  `SYS1.**NOTHERE` and `SYS1.A**` are answered with
  `501 ** must be preceded and followed by a period or blank`, the text z/OS
  FTP gives; a run of three or more `*` is refused the same way. They used to
  match like a single `*` with a wider reach, so `**ASM` found the data set
  `ASM`. `**`, `**.ASM`, `SYS1.**.ASM` and `**.**` are accepted as before.
  (#344)
- **A parenthesis in a data set name is checked to the end of the name.** The
  check stopped reading at the first `(`, so what followed was never looked at
  in `CWD`, in the arguments of `LIST` and `NLST`, or in the member of `DELE`,
  `RNFR`, `RNTO` and the transfer commands. A member now has to be exactly
  `(NAME)` at the end of the name, spelled with the characters a qualifier may
  hold; anything else is refused with `501`. `LIST` and `NLST` accept a member
  only in the form `DSN(pattern)`. (#335)
- **`CWD`, `MKD` and `RMD` refuse a name with a member.** These commands act on
  a data set as a whole. `RMD 'DSN(MEMBER)'` used to ask the security product
  about a name no profile can cover. On some systems it then deleted the member
  and answered `250`; on others it was answered `550` and deleted nothing. All
  three now answer `501` before the security product, the catalog or IDCAMS are
  asked, so the access check only ever sees the data set name. (#339)
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
