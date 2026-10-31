# Changelog

Changes that matter to someone who installs or runs FTPD. The notes for earlier
releases are on their [release pages](https://github.com/mvslovers/ftpd/releases).

## 1.3.3 — unreleased

A patch release for 1.3.2: fixes only, no new function. It is cut from `main`.
SMP FMID `TFTP133`, which deletes `TFTP132`, `TFTP131` and `TFTP130`: a system
on 1.3.0, 1.3.1 or 1.3.2 installs it directly. A system on 1.2.0 or earlier
installs 1.3.0 first.

**Read this first: the layout of `LIST JOBnnnnn` changes.** In the JES view, the
list of the spool files of a job now has the layout of z/OS. A script that reads
the old layout must be changed (see the first entry under "Fixed"). The list of
the jobs, `LIST` without a job ID, is unchanged.

### Fixed

- **`LIST JOBnnnnn` answers in the layout of z/OS.** In the JES view, `LIST`
  with a job ID answered with a table of its own: no job line, no line of
  dashes, no ID column and no count, and every line began with the job name and
  the job ID. A client that reads the layout of z/OS found no table in it, so
  the Zowe CLI plug-in for z/OS FTP reported `No spool file.` for a job that
  had four. The list now has the job line, a line of dashes, the table with an
  ID column and, as its last line, the number of files:

  ```
  JOBNAME  JOBID    OWNER    STATUS CLASS
  PAYROLL  JOB00038 ALICE    OUTPUT  A   RC=0000
  --------
          ID  STEPNAME PROCSTEP C DDNAME   BYTE-COUNT
          001 JES2              H JESMSGLG        931
          002 JES2              H JESJCL          810
          003 JES2              H JESYSMSG       3192
          004 STEP1             H SYSPRINT        484
          005 STEP1             H SYSUT2           80
  5 spool files
  ```

  The ID is the number that `RETR` takes after the point
  (`RETR JOB00038.3`), counted from 1 and printed with three digits. The list
  shows the files that `RETR` can send and no others, so the input files of the
  job (`JESJCLIN`, `JESINTXT` and the `SYSIN` data) are gone from it and it has
  fewer lines than before. The files that JES2 wrote itself have the step
  `JES2`. A job without such a file (still in the input queue, or its output
  purged) gets the header and the job line alone. The last line says `files`
  also for one file. `RETR` is unchanged. (#370)

- **The Zowe CLI plug-in for z/OS FTP works without a JESSPOOL profile.** The
  plug-in sends `SITE JESOWNER=*` before every job access, and FTPD answered
  `501 JESOWNER is not permitted` to a user who had no right to read the jobs
  of all users. The plug-in stopped at that reply, so without a profile of the
  class `JESSPOOL` (and on a RAKF that has no such class, where the profile
  cannot be defined) a user could not list or read the spool files of their own
  job. `SITE JESOWNER=*`, and `SITE JESOWNER=` followed by the ID of another
  user, are now answered `200` with a line that says the user may not filter on
  that owner, as z/OS FTP does, and the filter stays at the user's own ID. A
  script that took the `501` as the sign of a missing right must read the text
  of the `200` instead. A value that is not a user ID (a pattern such as
  `ABC*`, more than eight characters, a character other than a letter, a
  digit, `$`, `#` or `@`) is still answered `501`. The jobs of other users stay
  hidden in `LIST`, `RETR` and `DELE` as before, because each job is checked on
  its own, whatever the filter says. A user who has the right to read the jobs
  gets the filter as before. (#398)

- **A comment card is no longer taken for the JOB statement.** When a job is
  submitted in the JES view, FTPD adds `NOTIFY`, `USER` and `PASSWORD` behind
  the JOB statement. It found the statement as the first card with the word JOB
  between blanks, so a comment card such as `//* RUN THE JOB NOW` before the
  real JOB statement was taken for it. The three operands went behind the
  comment, the real JOB statement got none, and the job ran without an owner
  instead of under the identity of the user. FTPD now takes a card for the JOB
  statement only if it starts with `//`, has a name of one to eight characters
  (letters, digits, `@`, `#`, `$`), one or more blanks, and the operation `JOB`
  followed by a blank or the end of the card. A card without a name is not a
  JOB statement. Comment cards and any other card before it are passed on to
  JES unchanged. Only the first JOB statement of a stream gets the operands; a
  later JOB card is passed on as it is. (#277)

- **`USER` and `PASSWORD` go on the JOB statement, or the job is refused.** When
  a job is submitted in the JES view, FTPD puts `USER` and `PASSWORD` on
  continuation cards behind the JOB statement, and `NOTIFY=DUMMY` as well when
  the statement has no `NOTIFY` of its own. It took the last card of the JOB
  statement from the last non-blank character of a card alone and put the
  comma that continues the statement there, and only if the card was shorter
  than 71 characters. Three shapes of input put the cards behind a card that
  is not part of the statement. A comment behind the operands, as in
  `//J JOB (A),'T',CLASS=A   NOTE, MORE`, got the comma behind its last
  word, so the comma stood in the comment and continued nothing. A comment
  that ended in a comma (`NOTE,`) was taken for a continuation, so the cards
  went behind the card that followed. A JOB card that reached column 71 had
  no room for the comma. The cards were then statements of their own. The job
  ended with a JCL error, had no owner, and the `PASSWORD=` card stood in its
  JCL listing in the spool.

  FTPD now ends the statement as JCL does. The operand field ends at the first
  blank outside a quoted string. Only a comma as the last character of
  the operand field continues the statement, and a comment card between
  continuation cards does not end it. The comma goes directly behind the
  operand field, and the cards that follow it are the credentials. FTPD holds
  the cards of the statement back until its end is known. When the statement
  cannot take the credentials, no card of it is put to JES2, the job is not
  submitted, and the answer is

  ```
  550 JOB statement cannot be continued with the credentials; job not submitted
  ```

  That is the case when the operand field ends in column 71 or beyond (no room
  for the comma), when a card ends inside a quoted string, when a parenthesis
  is still open at the end of the operand field (a blank inside the parentheses
  ends the field), when the card after a comma is not a continuation card
  (`//`, a blank, and operands that begin at column 16 at the latest), when the
  stream ends inside the statement, and when the statement has more than 16
  cards. For a refused statement the password is never written to the spool.
  A comma in the comment field and a comment card between continuation cards
  are submitted with the credentials on the JOB statement. (#383, #386)

- **`PASV` and `EPSV` go on with the next port when a session lost the race for
  one.** FTPD walks the range of `PASVPORTS` and takes the first port that
  `bind()` accepts, and calls `listen()` on it afterwards. When several
  sessions asked for a passive port at the same moment, two of them could be
  handed the same port; the second `listen()` failed with `EADDRINUSE`, and the
  session was answered `425 Cannot open passive connection` although the range
  had free ports. FTPD wrote `ftpd_data_pasv: listen() failed, errno=48` to the
  log for each. A `listen()` that fails with `EADDRINUSE` now counts as a port
  that is taken, like a failed `bind()`: FTPD closes the socket and tries the
  next port. `425` comes only when the whole range is in use, and any other
  `listen()` error still ends the walk at once. (#315)

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
  them. A burst of more clients than that lost some of them: the client's
  connect succeeded, and the `220` came late or not at all. FTPD now asks for
  128. Above `MAXSESSIONS`, the rest is still closed at once without a reply.
  The 128 is a request: the host limits the queue with a limit of its own.
  (#327)

- **A login is refused when the ACEE does not belong to the user who logged
  in.** After the security system had accepted a login, FTPD took the result
  for granted. FTPD now compares the user of the returned ACEE with the user ID
  of the login and answers `530 Login incorrect.` when they differ. The login
  is logged as a warning and counts as a failed attempt. The ACEE of FTPD's own
  started task is never released.

- **FTPD checks the security system once at start.** An authorized FTPD (see
  `FTPD008I`) logs in once with a user ID and a password that cannot be valid.
  A refusal is the expected answer and changes nothing, except that the
  security system writes one line for the refused login to the console. When
  the login is accepted instead, FTPD writes the new message `FTPD004E` (two
  lines) to the console and answers every login with `530 Login not possible:
  the security check of this server is not working. Ask the administrator to
  check the FTPD004E console message.` FTPD stays up. The check is made only at
  start: after the cause is corrected, restart FTPD. There is no setting for it.

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
