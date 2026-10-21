#import "../bookmaster/bookmaster.typ": *

= The SITE Command <ref-site>

#idx("SITE", "keywords")
#cmd("SITE") sets parameters of the session. This chapter lists every keyword.
The _FTPD User's Guide_ explains what they are for, in "Setting Up a Session
with SITE".

== Syntax <ref-site-syntax>

```
SITE [keyword[=value] ...]
```

- FTPD splits the argument at blanks and tabs. Each word is one keyword, with
  or without #cmd("=") and a value. A word of more than 95 characters is cut.
- The verb, the keyword and the value are not case sensitive. FTPD folds the
  keyword to upper case. It folds the values of #cmd("RECFM"), #cmd("VOLUME"),
  #cmd("UNIT"), #cmd("FILETYPE"), #cmd("JESJOBNAME"), #cmd("JESOWNER") and
  #cmd("JESSTATUS") too. This holds since FTPD 1.3.0 for the three JES values.
- A keyword is at most 31 characters and a value at most 63 characters. FTPD
  cuts what is longer, without a message.
- A keyword may be shortened (@ref-site-abbr). FTPD refuses a form that is too
  short.
- FTPD applies the words from left to right. A word that is wrong does not
  stop the others.
- A session starts with the values of the #cmd("DEF") parameters
  (@ref-prm-def). A keyword that has such a parameter, given with no value,
  returns to it.

== Reply <ref-site-reply>

#tab(caption: [Replies to SITE])[
  #table(columns: (2.6in, 1fr),
    [Reply], [When],
    [#cmd("200 SITE command was accepted")], [All words are known and have good
      values.],
    [#cmd("200-Unrecognized parameter 'WORD' on SITE command.") and
      #cmd("200 SITE command was accepted")], [At least one word is not a
      keyword, or is a form of a keyword that is too short (@ref-site-abbr).
      The line names the first, as the client typed it, with its value. The
      other words still apply.],
    [#cmd("200-text") and #cmd("200 SITE command was accepted")], [A value was
      out of range or refused. The text names the first problem
      (@ref-site-keys).],
    [#cmd("202 SITE not necessary; you may proceed")], [No word.],
  )
]

The code is #cmd("200") in all cases but the last. A script must read the first
line.

== Abbreviations <ref-site-abbr>

#idx("SITE", "abbreviations")#idx("abbreviations")
Each keyword has a shortest form. FTPD takes a word as a keyword when the part
before the equals sign, folded to upper case, is the full name or the start of
the full name and at least as long as the shortest form. So #cmd("PRI"),
#cmd("PRIM") and #cmd("PRIMARY") are one keyword, and #cmd("PR") is none. The
full name always wins over a shorter keyword that it starts with, which keeps
#cmd("BLOCKS") apart from #cmd("BLOCKSIZE").

#tab(caption: [The shortest form of each SITE keyword])[
  #table(columns: (1.8in, 1.2in, 1fr),
    [Keyword], [Shortest form], [Remark],
    [#cmd("BLKSIZE")], [#cmd("BLK")], [],
    [#cmd("BLOCKSIZE")], [#cmd("BLOCKSI")], [The same keyword as
      #cmd("BLKSIZE").],
    [#cmd("BLOCKS")], [#cmd("BL")], [#cmd("BLO") and #cmd("BLOC") are
      #cmd("BLOCKS") too, not the block size.],
    [#cmd("CYLINDERS")], [#cmd("CY")], [],
    [#cmd("DATACLASS")], [#cmd("DATAC")], [],
    [#cmd("DIRECTORY")], [#cmd("DIR")], [],
    [#cmd("FILETYPE")], [#cmd("FILE")], [],
    [#cmd("JESJOBNAME")], [#cmd("JESJOBN")], [],
    [#cmd("JESOWNER")], [#cmd("JESO")], [],
    [#cmd("JESSTATUS")], [#cmd("JESST")], [],
    [#cmd("LRECL")], [#cmd("LR")], [],
    [#cmd("MGMTCLASS")], [#cmd("MG")], [],
    [#cmd("PRIMARY")], [#cmd("PRI")], [],
    [#cmd("RDW")], [#cmd("RDW")], [No shorter form.],
    [#cmd("RECFM")], [#cmd("REC")], [],
    [#cmd("RETPD")], [#cmd("RET")], [],
    [#cmd("SBSENDEOL")], [#cmd("SBSENDEOL")], [No shorter form.],
    [#cmd("SECONDARY")], [#cmd("SEC")], [],
    [#cmd("STORCLASS")], [#cmd("STO")], [],
    [#cmd("TRACKS")], [#cmd("TR")], [],
    [#cmd("TRAILINGBLANKS")], [#cmd("TRAIL")], [#cmd("TRAILING") is a longer
      form of it and works.],
    [#cmd("TRUNCATE")], [#cmd("TRUN")], [],
    [#cmd("UNIT")], [#cmd("UNI")], [],
    [#cmd("VOLUME")], [#cmd("VOL")], [],
  )
]

The table shows what FTPD accepts. It does not claim that these are the
shortest forms of the z/OS server.

A word that is too short, that fits no keyword, or that would fit two, is
answered like any word that is not a keyword (@ref-site-reply): the reply names
it with #cmd("Unrecognized parameter") and no setting changes for that word.
The text in the reply is the word as the client typed it, with its value, as
#cmd("Unrecognized parameter 'PR=5' on SITE command."). With the keywords of
this version no form fits two keywords. The names of #cmd("DATACLAS"),
#cmd("STORCLAS") and #cmd("MGMTCLAS") that FTPD took before 1.3.0 are starts
of the full names, and still work.

== Keywords <ref-site-keys>

#tab(caption: [SITE keywords])[
  #table(columns: (1.2in, 1fr),
    [Keyword], [Effect and rules],
    [#cmd("RECFM=f")], [The record format of a new data set. Upper case. No
      check. No value: the value of #cmd("DEFRECFM").],
    [#cmd("LRECL=n")], [The record length. FTPD converts the value as a number
      and does not check it. No value: #cmd("DEFLRECL").],
    [#cmd("BLKSIZE=n"), #cmd("BLOCKSIZE=n")], [The block size. Both names are one
      keyword. If the record format starts with
      #cmd("F"), the record length is above 0 and the block size is not a
      multiple of it, FTPD rounds the block size down to a multiple (at least
      one record) and sends the text #cmd("BLOCKSIZE must be a multiple of LRECL for RECFM FB"). No value: #cmd("DEFBLKSIZE").],
    [#cmd("PRIMARY=n")], [The primary quantity, 1 to 16777215. Another value:
      the setting stays and the text is #cmd("PRIMARY must be 1-16777215; keeping the current value"). No value: #cmd("DEFPRIMARY").],
    [#cmd("SECONDARY=n")], [The secondary quantity, 0 to 16777215. Another
      value: #cmd("SECONDARY must be 0-16777215; keeping the current value").
      No value: #cmd("DEFSECONDARY").],
    [#cmd("DIRECTORY=n")], [The directory blocks for #cmd("MKD"), 1 to 16777215.
      The value must be a whole decimal number. Another value: the setting stays
      and the text is #cmd("DIRECTORY must be 1-16777215; keeping the current value"). No value: #cmd("DEFDIRECTORY") (10 unless the
      parameters say otherwise). The number is fixed when the data set is
      created. A member #cmd("STOR") into a full directory answers
      #cmd("552") (@ref-reply-552).],
    [#cmd("RETPD=n")], [The retention period of a new data set in days, 0 to
      9999. The value must be a whole decimal number. 0 is no retention. It
      applies as @ref-site-retpd says. Another value: the setting stays and
      the text is #cmd("RETPD must be 0-9999; keeping the current value"). No
      value: no retention. A session starts without one, and there is no
      #cmd("DEF") parameter for it.],
    [#cmd("TRACKS")], [The unit of the quantities is the track.],
    [#cmd("CYLINDERS")], [The unit of the quantities is the cylinder.],
    [#cmd("BLOCKS")], [The unit is the block. With a block size of 0 or less:
      the unit stays, and the text is #cmd("BLOCKS needs a non-zero BLKSIZE; allocation unit unchanged").],
    [#cmd("VOLUME=v")], [The volume for a new data set. Upper case. FTPD checks
      that the volume is mounted. If not: the setting stays, and the text is
      #cmd("VOLUME is not mounted; keeping the current value"). No value:
      #cmd("DEFVOLUME").],
    [#cmd("UNIT=u")], [The unit for a new data set. Upper case. No check. No
      value: #cmd("DEFUNIT").],
    [#cmd("FILETYPE=t")], [#cmd("JES") (case does not matter) switches to the
      JES view and keeps the view that was active. Any other value, or none,
      ends the JES view and returns to the kept view.],
    [#cmd("TRAILINGBLANKS")], [A switch. Each use turns on or off the sending of the
      blanks at the end of a record in text mode.],
    [#cmd("JESJOBNAME=n")], [The job name for the job list. Upper case.
      #cmd("JESJOBNAME=") with no value ends the filter and
      lists all job names.],
    [#cmd("JESOWNER=n")], [The owner for the job list. Upper case. #cmd("*")
      means all owners. Without it the owner is the user of the session.],
    [#cmd("JESSTATUS=s")], [Accepted and stored in upper case. The job list does
      not use it.],
    [#cmd("TRUNCATE"), #cmd("RDW")], [Switches that are accepted. No part of
      FTPD reads them.],
    [#cmd("SBSENDEOL")], [Accepted. It does nothing.],
    [#cmd("DATACLASS"), #cmd("STORCLASS"), #cmd("MGMTCLASS")], [Accepted and
      ignored, with any value. MVS 3.8j has no SMS.],
  )
]

Every other word is #cmd("Unrecognized parameter"). That includes
#cmd("JESINTERFACELEVEL"), which is a parameter of the configuration member
(@ref-prm) and not a keyword of #cmd("SITE").

== RETPD <ref-site-retpd>

#idx("RETPD")#idx("retention period")
#cmd("SITE RETPD=n") gives the data sets that the session creates a retention
period of _n_ days. FTPD passes it to the allocation of:

- a partitioned data set that #cmd("MKD") creates, and
- a sequential data set that #cmd("STOR") or #cmd("APPE") creates because the
  name does not exist.

It does not apply to

- a member that #cmd("STOR") or #cmd("APPE") adds to a partitioned data set,
- a data set that exists: FTPD sends no retention period for it,
- the UFS and JES views.

The value stays for the session. #cmd("SITE RETPD=0") and #cmd("SITE RETPD")
with no value both switch it off.

If the system refuses the allocation with the retention period, the reply to
#cmd("STOR") is #cmd("550 Cannot allocate dataset NAME") and FTPD removes the
data set that it had created for the transfer.

=== STOR and APPE over a Retained Data Set <ref-site-retained>

#idx("retention period", "STOR and APPE")#idx("IEC507D")#idx("550", "retained data set")
A data set that was allocated with a retention period carries an expiration
date in its label. Before #cmd("STOR") or #cmd("APPE") opens an existing data
set of the MVS view for output, FTPD reads that date. When the date is later
than today, FTPD does not open the data set. The reply is a #cmd("550"), and
the session goes on with the next command:

#tab(caption: [Replies for a retained data set])[
  #table(columns: (3.2in, 1fr),
    [Reply], [When],
    [#cmd("550 NAME is retained until YYYY-MM-DD (YYYY.DDD) and is not opened for writing before then.")],
      [#cmd("STOR"). The date is given as year, month and day, and as year
      and day of the year.],
    [#cmd("550 NAME is retained until YYYY-MM-DD (YYYY.DDD) and is not opened for appending before then.")],
      [#cmd("APPE"), the same.],
    [#cmd("550 NAME has a permanent retention period and is not opened for writing.")],
      [#cmd("STOR") over a data set with the expiration date 99365 or 99366,
      which never expires. For #cmd("APPE") the last word is
      #cmd("appending").],
  )
]

- *The date.* A data set expires on its expiration date. On that day
  #cmd("STOR") and #cmd("APPE") open it, and from the next day on they get
  the #cmd("550"). FTPD compares with the date of the local clock of the
  system.
- *The label decides, not the session.* A session without
  #cmd("SITE RETPD") gets the same reply for a data set that another session
  or a job allocated with a retention period.
- *A member.* The label of the partitioned data set decides, and the reply
  names the partitioned data set, not the member. A partitioned data set that
  #cmd("MKD") created under #cmd("SITE RETPD") takes no member until it
  expires.
- *The order of the checks.* The name and the security check come first. Then
  come the #cmd("APPE") refusals for a member that exists and for a
  partitioned data set without a member (_FTPD User's Guide_, "Appending").
  The retention check follows them and runs before FTPD allocates or opens
  anything.
- *Not checked.* #cmd("DELE") and #cmd("RMD") do not look at the expiration
  date. On the system where this was measured, both removed a retained data
  set (#cmd("250 NAME deleted.")).
- *No label, no check.* When FTPD cannot read the label, it goes on as it
  does for a data set without retention.

A server of FTPD 1.2.0 or earlier makes no such check. The system asks the
operator with message #cmd("IEC507D") before it opens the data set, and the
session waits for the answer.
