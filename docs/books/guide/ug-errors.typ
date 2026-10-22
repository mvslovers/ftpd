#import "../bookmaster/bookmaster.typ": *

= Typical Errors and What They Mean <ug-errors>

#idx("errors")#idx("reply codes", "errors")
Every task chapter of this book shows the errors that belong to its task. This
chapter collects the ones that a user meets most often, says what each one
means, and sends the reader to the section that has the session. A reply that
is not here is in "Reply Codes" in the _FTPD Reference_. How to read the first
digit of a code is in @ug-first-replies.

== Names and the Working Directory <ug-errors-names>

#idx("name", "errors")
The most common cause of a #cmd("550") is a name that FTPD read in another way
than the reader meant. An unquoted name is a name below the working directory,
and a name in single quotes is a full name (@ug-data-names). The session shows
the replies to names that do not exist, to names that are not valid, and to a
working directory that does not exist:

#screen(raw(read("../ex/ug-data/d-errors.txt"), block: true))

- *#cmd("550 Data set IBMUSER.NOSUCH not found"):* the first #cmd("get") had no
  quotes, so FTPD put the working directory in front of the name. The reply
  names the data set that it looked for. When this is not the one the reader
  meant, the working directory or the quotes are wrong. The second #cmd("get")
  had the full name in quotes, and the data set is not there.
- *#cmd("501 A qualifier in \"TE*\" contains an invalid character"):* a name
  that is a pattern cannot be fetched. A #cmd("*") is a pattern character only
  for a list and for #cmd("mget") (@ug-data-list).
- *#cmd("501 Invalid data set name ..."):* the name has a character that a data
  set name cannot have. *#cmd("501 Invalid dataset name"):* the whole name is
  longer than 44 characters, or no member name is left.
- *#cmd("250 \"IBMUSER.NOSUCH.\" is the working directory name prefix."):* a
  #cmd("cd") to a prefix that is not cataloged works. FTPD does not check the
  prefix. The first command that uses it gets the reply, here
  #cmd("550 No data sets found.") for #cmd("ls").

== Errors by Task <ug-errors-table>

The tables name the reply, the cause, and what the reader does about it. The
last column is the section with the session.

#tab(caption: [Errors when logging in])[
  #table(columns: (1.8in, 1fr, 1.5in),
    [Reply], [Cause and remedy], [Section],
    [#cmd("530 Login incorrect.")],
      [The user ID or the password is wrong. The reply is the same for an
      unknown user ID. Type #cmd("user") again. The third failed attempt ends
      the connection.], [@ug-first-fail],
    [#cmd("530 Not authorized for FTP access.")],
      [The password is right, but the user ID may not use FTP. Ask the
      administrator.], [@ug-first-fail],
    [#cmd("530 Not logged in.")],
      [The command needs a login. Log in first.], [@ug-first-fail],
    [#cmd("421 Idle timeout, closing connection")],
      [The client sent nothing for the idle time (300 seconds by default).
      Connect again.], [@ug-first-leave],
    [#cmd("421 Service not available ...")],
      [FTPD is shutting down, or has no storage for the session. Try again
      later or ask the operator.], [@ug-ops-ses],
  )
]

#tab(caption: [Errors when reading and listing data sets])[
  #table(columns: (1.8in, 1fr, 1.5in),
    [Reply], [Cause and remedy], [Section],
    [#cmd("550 Data set NAME not found")],
      [The data set is not cataloged under that name. Check the working
      directory and the quotes.], [@ug-errors-names],
    [#cmd("550 Request nonexistent member NAME(MEMBER) to be sent.")],
      [The partitioned data set has no such member. List it first.],
      [@ug-data-pds],
    [#cmd("550 No data sets found.")],
      [Nothing is cataloged under the working directory, or the partitioned
      data set has no member that fits.], [@ug-data-list],
    [#cmd("451 Insufficient storage to list ...")],
      [The started task is out of storage. Narrow the directory, or ask the
      operator to raise the region.], [@ug-data-list],
    [#cmd("426 Transfer aborted: data connection lost ...")],
      [The connection broke during the transfer. What the client has is
      incomplete. Start again.], [@ug-data-retr],
  )
]

#tab(caption: [Errors when writing data sets])[
  #table(columns: (1.8in, 1fr, 1.5in),
    [Reply], [Cause and remedy], [Section],
    [#cmd("552 Requested file action aborted: out of space ...")],
      [The allocation was too small. Allocate more with #cmd("site primary"),
      #cmd("site secondary") and #cmd("site cylinders"). A retry with the same
      allocation fails again.], [@ug-data-space],
    [#cmd("552 ... the directory of the PDS is most likely full ...")],
      [The partitioned data set has no room for another member. Create a new
      one with more directory blocks and copy the members.],
      [@ug-data-dirfull],
    [#cmd("451 File transfer failed. File contains records that are longer than the LRECL ...")],
      [A text line is longer than the record length of the data set. Change the
      record length with #cmd("site lrecl"), or let FTPD cut the lines with
      #cmd("site truncate").], [@ug-data-long],
    [#cmd("451 Write error on data set after N bytes received ...")],
      [Any other write error. The data set is incomplete.], [@ug-data-space],
    [#cmd("550 NAME is retained until DATE ... and is not opened for writing before then.")],
      [The data set has not reached its expiration date and is not overwritten.
      Delete it, or store under another name.], [@ug-data-retained],
    [#cmd("550 APPE needs a sequential data set or a new member")],
      [#cmd("append") to a partitioned data set without a member. Name the
      member.], [@ug-data-manage],
    [#cmd("550 APPE cannot append to the existing member ...")],
      [A member cannot be extended. Fetch it, join the parts, and #cmd("put")
      the whole.], [@ug-data-manage],
    [#cmd("550 DELE fails: NAME does not exist.")],
      [There is no data set of that name below the working directory. Quote the
      full name.], [@ug-data-manage],
  )
]

#tab(caption: [Errors in the JES and UFS views])[
  #table(columns: (1.8in, 1fr, 1.5in),
    [Reply], [Cause and remedy], [Section],
    [#cmd("550 Jobid JOBnnnnn not found for JESJOBNAME=..., JESSTATUS=..., JESOWNER=...")],
      [JES2 does not know the job, the filter #cmd("JESSTATUS") excludes it,
      the ID has a #cmd("*") or a pattern, or the job belongs to another user
      and no profile of the class #cmd("JESSPOOL") lets you see it. The reply
      does not say which. Check the ID with #cmd("dir") and set the status back
      to #cmd("all").], [@ug-jes-output, @ug-jes-others],
    [#cmd("550 Access denied - cannot cancel JOBnnnnn")],
      [#cmd("delete") of a job of another user that you may read but not
      update. Ask the administrator for the right.], [@ug-jes-others],
    [#cmd("501 JESOWNER is not permitted")],
      [#cmd("site jesowner") named another user or #cmd("*") without the right to
      read their jobs, or the value is a pattern, longer than eight characters
      or has a character that is not allowed. The value is not repeated, and
      the filter stays as it was.], [@ug-jes-list, @ug-jes-others],
    [#cmd("550 Spool file n not found for Jobid ...")],
      [The number after the point is past the output files of the job. The reply
      says how many there are.], [@ug-jes-output],
    [#cmd("451 Unable to open JES checkpoint")],
      [FTPD cannot read the JES2 checkpoint. Ask the operator.],
      [@ug-jes],
    [#cmd("550 UFS service not available")],
      [UFSD is not running. Ask the operator.], [@ug-ufs],
    [#cmd("550 File or directory not found")],
      [The UFS path does not exist.], [@ug-ufs-manage],
    [#cmd("502 Rename not supported for UFS")],
      [A file cannot be renamed in UFS. Copy it and delete the old one.],
      [@ug-ufs-manage],
    [#cmd("552 No inodes available")],
      [The UFS file system has no free inode. A retry gets the same answer. Ask the
      administrator.], [@ug-ufs-errors],
  )
]

#tab(caption: [Errors with SITE and the client])[
  #table(columns: (1.8in, 1fr, 1.5in),
    [Reply or symptom], [Cause and remedy], [Section],
    [#cmd("200-...; keeping the current value")],
      [A value of #cmd("site") is wrong. The reply is a #cmd("200") whose first
      line names the problem. The other values of the same command are
      applied.], [@ug-site-form],
    [#cmd("501 ...")],
      [The server refuses the value of #cmd("site"), for example a retention
      period above #cmd("MAXRETPD"), #cmd("rdw"), a #cmd("DCBDSN") that is not
      cataloged or a #cmd("jesowner") that you may not use. The first line says
      which value and why, except for #cmd("jesowner"): its text is always
      #cmd("JESOWNER is not permitted").],
      [@ug-site-form],
    [#cmd("?Invalid command.")],
      [Not a reply of FTPD. In #cmd("tnftp") the command is #cmd("site") in lower
      case.], [@ug-site-case],
    [Login works, then the transfer hangs or fails],
      [The client cannot reach the passive port that FTPD named. A firewall
      has to let the ports of #cmd("PASVPORTS") through.], [@ug-first-client],
  )
]
