#import "../bookmaster/bookmaster.typ": *

= Working with Data Sets <ug-data>

#idx("data set")
A session starts in the MVS view: every name the client gives is the name of
an MVS data set or of a member of a partitioned data set. This chapter
describes how FTPD reads those names, what the working directory is, and what
the transfer commands do with data sets. The commands and replies are listed
one by one in the _FTPD Reference_.

== Data Set Names and Quotes <ug-data-names>

#idx("data set", "name")#idx("quotes")
FTPD follows the conventions of the z/OS FTP server for names:

- *A name in single quotes is absolute.* #cmd("'SYS1.PARMLIB'") is the data
  set #cmd("SYS1.PARMLIB"), whatever the working directory is.
- *A name without quotes is relative.* FTPD puts the working directory in
  front of it. With the working directory #cmd("IBMUSER."), the name
  #cmd("TEST.DATA") is the data set #cmd("IBMUSER.TEST.DATA").
- *Names are upper case.* FTPD folds what the client sends to upper case and
  drops a period at the end.
- *The longest name is 44 characters.* A name holds the letters #cmd("A") to
  #cmd("Z"), the digits, #cmd("@"), #cmd("\#"), #cmd("\$") and #cmd("-"),
  and the periods between the qualifiers. No qualifier may be empty.

A name that is too long is answered with #cmd("501 Invalid dataset name").
A name with a character a data set name cannot hold, such as the slash of a
local path that the client sent as it is, is answered with the name FTPD
built from it:

```
501 Invalid data set name "'IBMUSER.BUILD/X.XMIT'".  Use MVS Dsname conventions.
```

#idx("wildcards")
The characters #cmd("*"), #cmd("%") and #cmd("?") make a name a pattern.
Only #cmd("LIST") and #cmd("NLST") accept a pattern (@ug-data-list). Every
other command answers #cmd("501") and names the qualifier that carries the
character:

```
501 A qualifier in "TE*" contains an invalid character
```

== The Working Directory <ug-data-cwd>

#idx("working directory")#idx("CWD")
The working directory in the MVS view is a prefix, not a directory.
#cmd("CWD") sets it:

- After login it is the user ID and a period, such as #cmd("IBMUSER.").
- #cmd("CWD TEST") adds a qualifier to the prefix\; #cmd("CWD 'SYS1.MACLIB'")
  replaces it. #cmd("CWD ..") and #cmd("CDUP") remove the last qualifier,
  and at the top level they go back to the user ID. #cmd("CWD .") changes
  nothing.
- A name that ends in a period, as in #cmd("CWD 'SYS1.'"), is taken as a
  prefix as it is. FTPD does not look it up.
- For any other name FTPD checks the catalog. When the name is a partitioned
  data set, the reply says so:

  ```
  250 The working directory "SYS1.MACLIB" is a partitioned data set
  ```

  Otherwise the name becomes the prefix, with a period added, and FTPD
  does not require that a data set of that name exists:

  ```
  250 "IBMUSER.TEST." is the working directory name prefix.
  ```

#idx("partitioned data set", "working directory")
Inside a partitioned data set an argument without quotes is a member name
(@ug-data-pds). #cmd("PWD") shows which case applies:

#fig(caption: [What PWD answers])[
```
257 "'IBMUSER.TEST.'" is working directory.
257 "'SYS1.MACLIB'" partitioned data set is working directory.
```
]

#note[*PWD reports a data set name in quotes.* The reply puts the name in
single quotes inside the double quotes, as the z/OS server does. A client
that parses the path out of the reply has to strip both.]

== Partitioned Data Sets and Members <ug-data-pds>

#idx("partitioned data set")#idx("member")
There are two ways to name a member:

- *With the working directory in the partitioned data set.* After
  #cmd("CWD 'IBMUSER.SRC'"), every argument without quotes and without a
  parenthesis is a member name. FTPD makes it valid: a path before the last
  slash is dropped, and so is everything from the first period on (a file
  extension that the client adds). The rest is folded to upper case and cut
  to eight characters. So #cmd("STOR hello.c") and #cmd("STOR HELLO") both
  store the member #cmd("HELLO").
- *With the data set and the member in the name*, as in
  #cmd("'IBMUSER.SRC(HELLO)'"). FTPD makes the member name valid in the
  same way.

A member can only be stored into a partitioned data set that exists.
Otherwise FTPD answers:

```
550 IBMUSER.SRC(HELLO) requests a nonexistent partitioned data set. Use MKD command to create it.
```

#cmd("MKD") creates the partitioned data set (@ug-data-manage).

== Listing <ug-data-list>

#idx("LIST")#idx("NLST")
#cmd("LIST") sends one line per data set, in the form of the z/OS server:
volume, unit, last reference date, extents, tracks used, record format,
record length, block size, organization and name. The name is shortened by
the working directory prefix. Inside a partitioned data set #cmd("LIST")
sends one line per member, with the ISPF statistics that the member has.
#cmd("NLST") sends the names only.

A name given to #cmd("LIST") or #cmd("NLST") is a filter. Inside a
partitioned data set it filters the member names. Outside, FTPD reads the
catalog level of the working directory and keeps the names that fit the
filter. A filter without a wildcard keeps the data set of that name and all
data sets below it. These wildcards work in a data set filter:

#deflist(width: 0.9in,
  [#cmd("*")], [any characters within one qualifier.],
  [#cmd("**")], [any number of qualifiers.],
  [#cmd("%")], [one character.],
)

#note[The catalog is read at the level of the working directory. A filter
can only keep names below it, so to list another high-level qualifier, change
the working directory to it first.]

The replies mean:

- *The working directory is a prefix that is not cataloged, or a partitioned
  data set has no members that fit:* #cmd("550 No data sets found."). When the
  prefix is cataloged but nothing below it fits the filter, the list is empty
  and the reply is #cmd("226 List completed successfully.").
- *FTPD ran out of storage while it built the list:*
  #cmd("451 Insufficient storage to list IBMUSER."), with the prefix or the
  partitioned data set in place of #cmd("IBMUSER"). This is a shortage in the
  started task, not a missing prefix. Ask the operator to raise the region of
  the started task, or narrow the working directory and ask again.

Since FTPD 1.3.0 the reply #cmd("451") also covers the list of a catalog
level and the look-up of the record format for a member list. Before, a
shortage in the level list was taken for an empty level, and the answer was a
header and no entries. A shortage in the look-up of the record format was
ignored, and the listing went on. FTPD 1.2.0 already answered #cmd("451") when
it ran out of storage while it read the directory of a partitioned data set.

== Fetching <ug-data-retr>

#idx("RETR")
#cmd("RETR") sends a sequential data set or a member. FTPD checks that the
user may read the data set (@ug-security-dsn), sends a #cmd("125") reply
with the record format and the record length, sends the data, and ends with
#cmd("250 Transfer completed successfully."):

```
125 Sending data set IBMUSER.TEST.DATA FIXrecfm 80
```

The data depends on the transfer type:

#deflist(width: 0.9in,
  [#cmd("TYPE A")], [text. This is the type a session starts with. FTPD
    translates each record from EBCDIC (code page 037) to ASCII, drops the
    blanks at the end of the record, and ends it with CR and LF.
    #cmd("SITE TRAILING") keeps the blanks.],
  [#cmd("TYPE I")], [binary. FTPD sends the records as they are, one after
    the other, with no end-of-line marks.],
  [#cmd("TYPE E")], [EBCDIC. The same bytes as for binary.],
)

The error replies are:

- *The data set or member does not exist:*
  #cmd("550 Data set NAME not found") for a data set and
  #cmd("550 Request nonexistent member NAME(MEMBER) to be sent.") for a
  member.
- *The data connection breaks during the transfer:*
  #cmd("426 Transfer aborted: data connection lost after N bytes.")
- *The data set cannot be read to the end:* #cmd("451 Read error on data set after N bytes: transfer is incomplete.") The client holds a part of the data
  set.

== Storing <ug-data-stor>

#idx("STOR")
#cmd("STOR") writes the data it receives to a data set or to a member. A data
set that does not exist is created, with the attributes of the session
(@ug-site). A data set or a member that exists is replaced. FTPD checks that
the user may update the data set, sends #cmd("125 Storing data set NAME"),
reads the data and ends with #cmd("250 Transfer completed successfully.")
when every record has reached the data set.

How the records are made depends on the transfer type:

- *TYPE A:* each line becomes one record, translated from ASCII to EBCDIC
  (code page 037). For a fixed record format the record is padded with
  blanks.
- *TYPE I and TYPE E:* the bytes are cut into records of the record length,
  or of the block size for record format U. A last short record is padded
  with binary zeros (not for record format U).

#idx("allocation")
A data set that STOR creates is a sequential data set. FTPD allocates it
with the record format, record length, block size and space of the session,
writes it, and at the end releases the space that was not used (the data set
is opened with the #cmd("RLSE") option). So a generous primary quantity costs
nothing once the transfer has finished. A data set that existed is not
shortened.

=== When the Space Is Not Enough <ug-data-space>

#idx("space", "out of")#idx("552")
A client does not tell FTPD how large the data is, so the allocation is a
guess. When it is too small, the write fails and FTPD answers:

```
552 Requested file action aborted: out of space on the data set. Allocate more space with SITE PRIMARY/SECONDARY and CYLINDERS, or ask the administrator to raise DEFPRIMARY.
```

When the system ends the write with a space abend (B37, D37 or E37) instead
of reporting the error, the reply is the same with the abend code in it, as
in #cmd("out of space on the data set (ABEND SB37). Allocate more space ...").

The code is #cmd("552") and not #cmd("452") on purpose. A code in the 4xx
range tells a client to try again, and the same upload into the same space
fails the same way.

When this #cmd("STOR") created the data set, FTPD removes it again, so a retry
does not find a half-filled data set and fail again. A data set that existed
is left as the failed write left it. The client holds the only complete copy,
so keep it. Any other error while writing is answered with
#cmd("451 Write error on data set after N bytes received: the data set is incomplete."), and the same rule about the new data set applies.

Raise the space for the session with #cmd("SITE PRIMARY"),
#cmd("SITE SECONDARY") and #cmd("SITE CYLINDERS") (@ug-site), or ask the
administrator to raise #cmd("DEFPRIMARY") for everyone.

== Appending <ug-data-appe>

#idx("APPE")
#cmd("APPE") adds the data it receives to the end of a sequential data set.
Before FTPD 1.3.0 the command did what #cmd("STOR") does and replaced the data
set, so a client that appended with it lost the old content. Since FTPD 1.3.0
it appends. It follows the same rules as #cmd("STOR") for names, types,
records and errors, with these differences:

- *A sequential data set that exists:* the data is appended. FTPD sends
  #cmd("125 Appending to data set NAME").
- *A data set or a member that does not exist:* it is created, as
  #cmd("STOR") creates it, and the data is its first content.
- *A member that exists:* FTPD refuses, and the member is not touched:

  ```
  550 APPE cannot append to the existing member IBMUSER.SRC(HELLO): appending to a PDS member is not supported, the member is unchanged. Use STOR to replace it.
  ```
- *A partitioned data set named without a member:*

  ```
  550 APPE needs a sequential data set or a new member: IBMUSER.SRC is a partitioned data set.
  ```
- *The UFS view:* #cmd("502 APPE not supported for UFS").

FTPD makes these checks before it allocates or opens anything. A client that
wants to extend a member has to fetch it, join the parts and store the
whole.

== A Data Set Under Retention <ug-data-retained>

#idx("retention period")#idx("IEC507D")#idx("550", "retained data set")
A data set that was allocated with a retention period (@ug-site) must not be
overwritten before the period is over. #cmd("STOR") and #cmd("APPE") of an
existing data set check this before they open it. When the expiration date
has not come, FTPD refuses and names the date:

```
550 IBMUSER.IBMUSER.DOCS.RET is retained until 2026-10-22 (2026.295) and is not opened for writing before then.
550 IBMUSER.IBMUSER.DOCS.RET is retained until 2026-10-22 (2026.295) and is not opened for appending before then.
```

The first reply is for #cmd("STOR"), the second for #cmd("APPE"). The data set
is not touched, and the session works on. A data set with the expiration date
99365 or 99366 never expires. The reply for it is
#cmd("550 NAME has a permanent retention period and is not opened for writing.")

- A data set expires on its expiration date. On that day #cmd("STOR") and
  #cmd("APPE") work again.
- The expiration date is in the data set, so every session is refused, not
  only the one that used #cmd("SITE RETPD").
- A member is stored into a partitioned data set that exists, and its label
  decides. A partitioned data set that #cmd("MKD") created under
  #cmd("SITE RETPD") takes no member, new or existing, until it expires. The
  reply names the partitioned data set.
- #cmd("DELE") and #cmd("RMD") do not check the date. To replace a retained
  data set, delete it, or store under a new name.
- A data set without retention, and one whose period is over, is stored as
  usual.

FTPD 1.2.0 and earlier do not check. There the system asks the operator
with message #cmd("IEC507D") when it opens a retained data set for output,
and the session waits for the reply. The _FTPD Reference_ has the texts and
the order of the checks, in "RETPD".

== Creating, Deleting and Renaming <ug-data-manage>

#idx("MKD")#idx("DELE")#idx("RMD")#idx("RNFR")
These commands work on data sets and members:

#deflist(width: 1.0in,
  [#cmd("MKD")], [creates a partitioned data set with the attributes of the
    session. #cmd("SITE DIRECTORY") sets the number of directory blocks
    (the value of #cmd("DEFDIRECTORY"), 10 if the administrator did not
    change it), and #cmd("SITE RETPD") the retention period. A name that exists is answered with
    #cmd("550 NAME already exists.").],
  [#cmd("DELE")], [deletes a data set, or a member when the name has one.],
  [#cmd("RMD")], [deletes a data set.],
  [#cmd("RNFR"), #cmd("RNTO")], [rename a data set or a member.],
)

A rename needs both commands, #cmd("RNFR") with the old name and
#cmd("RNTO") with the new name. #cmd("RNTO") must be the very next command:
FTPD forgets the old name when any other command comes between. A data set
is renamed to a data set that does not exist yet. A member is renamed within
its partitioned data set. FTPD does not turn a member into a data set or
the other way round, and it does not move a member to another partitioned
data set.

These commands need the authority to alter the data set
(@ug-security-dsn). The reply to a deletion or a rename that fails names
the reason and the return code of IDCAMS.
