#import "../bookmaster/bookmaster.typ": *

= Working with Data Sets <ug-data>

#idx("data set")
A session starts in the MVS view: every name the client gives is the name of
an MVS data set or of a member of a partitioned data set. This chapter goes
through the tasks of that view: naming a data set, looking around, downloading
and uploading, working with the members of a partitioned data set, moving
several data sets at once, and renaming and deleting. Each task has a session.
The sessions start after the login (@ug-first-login).

Where a client command appears, the protocol command that it sends stands next
to it. A different client may type a different command and send the same
protocol command. The table "Client Commands and Protocol Commands" in the
_FTPD Reference_ lists them all.

== Naming a Data Set <ug-data-names>

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

The quotes are the point that most often goes wrong. The session shows what
the working directory #cmd("SYS1.PARMLIB") means without and with quotes:

#screen(raw(read("../ex/ug-data/d-quotes.txt"), block: true))

Without quotes #cmd("cd") made #cmd("IBMUSER.SYS1.PARMLIB.") the prefix, and
#cmd("get IEASYS00") looked for a data set with that long name. With quotes
the working directory is the partitioned data set #cmd("SYS1.PARMLIB"), and
the same #cmd("get") fetches its member. The quotes are sent to FTPD as they
are, so on a client whose command line treats the quote as its own, escape it
or enter the name another way.

A name that is too long, or that has a character a data set name cannot hold,
is refused with a #cmd("501"); @ug-errors-names shows the replies. The
characters #cmd("*"), #cmd("%") and #cmd("?") make a name a pattern, but #cmd("?")
matches nothing in a listing, so use #cmd("%") for one character. Only
listing accepts a pattern (@ug-data-list). Every other command answers
#cmd("501") and names the qualifier that carries the character. A member in
parentheses follows rules of its own (@ug-data-parens).

== Changing the Working Directory <ug-data-cwd>

#idx("working directory")#idx("CWD")
The working directory in the MVS view is a prefix, not a directory. The client
command #cmd("cd") sends #cmd("CWD"), and #cmd("pwd") sends #cmd("PWD"):

#screen(raw(read("../ex/ug-data/d-names.txt"), block: true))

The rules are:

- After login the prefix is the user ID and a period, such as #cmd("IBMUSER."),
  and #cmd("dir") lists the data sets below it.
- #cmd("cd test") adds a qualifier to the prefix\; #cmd("cd 'SYS1.MACLIB'")
  replaces it. #cmd("cd ..") and #cmd("cdup") (#cmd("CDUP")) remove the last
  qualifier, and at the top level they go back to the user ID. #cmd("cd .")
  changes nothing.
- A name that ends in a period, as in #cmd("cd 'SYS1.'"), is taken as a prefix
  as it is. FTPD does not look it up.
- #cmd("cd") takes a prefix or a partitioned data set, never a member. A name
  with a parenthesis, such as #cmd("cd 'SYS1.PARMLIB(IEASYS00)'") or
  #cmd("cd 'SYS1.PARMLIB(IEASYS00'"), is answered with #cmd("501"), and the
  working directory stays as it was (@ug-data-parens).
- For any other name FTPD checks the catalog. When the name is a partitioned
  data set, the reply says so (the #cmd("cd 'SYS1.PARMLIB'") above).
  Otherwise the name becomes the prefix, with a period added, and FTPD does
  not require that a data set of that name exists. A #cmd("cd") to a name that
  does not exist is therefore not an error. The first #cmd("dir") in it is
  (@ug-errors-names).

#idx("partitioned data set", "working directory")
Inside a partitioned data set an argument without quotes is a member name
(@ug-data-pds). The reply of #cmd("pwd") shows which case applies:

#fig(caption: [What PWD answers])[
```
257 "'IBMUSER.TEST.'" is working directory.
257 "'SYS1.MACLIB'" partitioned data set is working directory.
```
]

#note[*PWD reports a data set name in quotes.* The reply puts the name in
single quotes inside the double quotes, as the z/OS server does. #cmd("tnftp")
prints it as #cmd("Remote directory: 'IBMUSER.TEST.'"). A client that parses
the path out of the reply has to strip the quotes.]

== Listing Data Sets and Members <ug-data-list>

#idx("LIST")#idx("NLST")
The client commands #cmd("dir") and #cmd("ls") send #cmd("LIST"), and
#cmd("nlist") sends #cmd("NLST"). #cmd("LIST") sends one line per data set, in
the form of the z/OS server: volume, unit, last reference date, extents,
tracks used, record format, record length, block size, organization and name.
The name is shortened by the working directory prefix. Inside a partitioned
data set #cmd("LIST") sends one line per member, with the ISPF statistics that
the member has (the session in @ug-data-cwd shows both).
#cmd("NLST") sends the names only.

A name given to the command is a filter. Inside a partitioned data set it
filters the member names, as #cmd("dir IEAS*") did in the session above.
Outside, FTPD reads the catalog and keeps the names that fit the filter. A
filter without a wildcard keeps the data set of that name and all data sets
below it. Which part of the catalog FTPD reads depends on the name
(@ug-data-list-abs). These wildcards work in a data set filter:

#deflist(width: 0.9in,
  [#cmd("*")], [a whole qualifier, or the rest of a qualifier. #cmd("OLD*")
    keeps #cmd("OLDREP") but not #cmd("OLD.DATA"). A #cmd("*") may not stand
    in front of letters: #cmd("*REP") does not match #cmd("OLDREP").],
  [#cmd("**")], [any number of qualifiers, also none. #cmd("OLD**") keeps #cmd("OLDREP") and
    #cmd("OLD.DATA").],
  [#cmd("%")], [one character.],
)

A name of the form #cmd("'DSN(pattern)'") is not a data set filter. #cmd("LIST")
and #cmd("NLST") answer it with the members of that partitioned data set that
fit the pattern, from any working directory, in the form that the listing
inside the data set has. The reply is #cmd("550 No data sets found.") when the
data set is not there, is not partitioned, or has no member that fits. Before
FTPD 1.3.1 the list of such a name was empty. The _FTPD Reference_, "LIST and
NLST", has the rules.

=== Absolute Names and Patterns <ug-data-list-abs>

#idx("LIST", "absolute name")#idx("NLST", "absolute name")#idx("catalog level")
A name in single quotes is read at its own place in the catalog, whatever the
working directory is. The session lists the data sets of #cmd("SYS2") from the
working directory #cmd("IBMUSER."), without a #cmd("cd") before:

#screen(raw(read("../ex/ug-data/d-absolute.txt"), block: true))

FTPD reads the catalog at the *level* of the name: the qualifiers in front of
the first qualifier that holds a wildcard. The filter then keeps what fits.

#tab(caption: [The catalog level that LIST and NLST read])[
  #table(columns: (1.5in, 1fr),
    [Name], [What FTPD reads and lists],
    [#cmd("'SYS2.PROCLIB'")], [The level #cmd("SYS2"). The result is the data
      set #cmd("SYS2.PROCLIB") and every data set below it.],
    [#cmd("'SYS2.**'")], [The level #cmd("SYS2"). The result is every data set
      below #cmd("SYS2"), with any number of further qualifiers.],
    [#cmd("'SYS2.PROC*'")], [The level #cmd("SYS2"). The result is the names
      below #cmd("SYS2") that fit.],
    [#cmd("'**'"), #cmd("'*.PROCLIB'")], [The first qualifier is a pattern, so
      there is no level in the name. FTPD reads the working directory and keeps
      what fits.],
    [#cmd("TEST.*") without quotes], [FTPD puts the working directory in
      front of the name. With #cmd("IBMUSER."), that is #cmd("IBMUSER.TEST.*")
      at the level #cmd("IBMUSER.TEST").],
  )
]

The name without a wildcard is read one level up because the catalog lists what
is below a name, and not the data set of that name. A one-qualifier name is
its own level. #cmd("LIST") and #cmd("NLST") shorten a name by the working
directory prefix, as above\; a name outside the working directory shows in full. As long
as the working directory is a partitioned data set, the argument is a member
filter, and a name like #cmd("'SYS2.PROCLIB'") answers
#cmd("550 No data sets found."). Change to a prefix first (@ug-data-cwd).

A listing shows names, not contents. FTPD does not check each entry of the list
against RAKF (@ug-security-dsn), also not for the data sets of another
high-level qualifier. #cmd("RETR") of a data set the user may not read is still
refused.

Some clients ask for an absolute name or pattern without a #cmd("CWD") before
it. Clients like the Zowe FTP plugin do so when they list data sets
(#cmd("zowe zftp ls ds")). FTPD 1.4.0 and later answer such a request as the
session shows. Earlier releases read only the working directory, and the list
stayed empty unless the working directory was the high-level qualifier of the
name.

The replies mean:

- *The level is not cataloged, or a partitioned data set has no members that
  fit:* #cmd("550 No data sets found."). For a data set level the reply means
  that nothing is cataloged below the level and the level is not a data set
  itself, as for #cmd("'NOSUCH.**'") in the session above. When the level is
  cataloged but nothing below it fits the filter, the list is empty and the
  reply is #cmd("226 List completed successfully."), as for
  #cmd("'SYS2.NOSUCH'").
- *FTPD ran out of storage while it built the list:*
  #cmd("451 Insufficient storage to list IBMUSER."), with the prefix or the
  partitioned data set in place of #cmd("IBMUSER"). This is a shortage in the
  started task, not a missing prefix. Ask the operator to raise the region of
  the started task, or narrow the working directory and ask again.

== Downloading a Data Set <ug-data-retr>

#idx("RETR")
The client command #cmd("get") sends #cmd("RETR"). FTPD checks that the user
may read the data set (@ug-security-dsn), sends a #cmd("125") reply with the
record format and the record length, sends the data, and ends with #cmd("250
Transfer completed successfully."). The session fetches a member of
#cmd("SYS1.PARMLIB") as text and as binary:

#screen(raw(read("../ex/ug-data/d-download.txt"), block: true))

#cmd("tnftp") sends two more commands around #cmd("get"), #cmd("SIZE") before
and #cmd("MDTM") after the #cmd("RETR"). FTPD answers both with a #cmd("502")
for a data set, and the client carries on. They do not show in the session.

The data depends on the transfer type. #cmd("ascii") sends #cmd("TYPE A") and
#cmd("binary") sends #cmd("TYPE I"):

#deflist(width: 0.9in,
  [#cmd("ascii")], [text. This is the type a session starts with. FTPD
    translates each record from EBCDIC (code page 037) to ASCII, drops the
    blanks at the end of the record, and ends it with CR and LF.
    #cmd("site trailingblanks") keeps the blanks. The first #cmd("get") above is
    text. The client reported 574 bytes, and the file on the local disk has
    567, one less for each of the 7 lines: the line end on the wire is CR and
    LF, and #cmd("tnftp") writes LF.],
  [#cmd("binary")], [binary. FTPD sends the records as they are, one after the
    other, with no end-of-line marks. The second #cmd("get") is binary: 560
    bytes, 7 records of 80 bytes. The #cmd("od") listing shows EBCDIC, not
    text: the byte #cmd("@") (40 in hex) is the EBCDIC blank.],
)

FTPD knows a third type, #cmd("TYPE E") for EBCDIC, which is the same as
binary for a data set. The _FTPD Reference_ describes it.

Use text for what is text. Use binary for what is not: a load module, an XMIT
file, a data set with packed fields. A binary download is exactly the bytes of
the data set, so keep the record length in mind if you send it back
(@ug-data-stor).

#idx("record format", "variable")#idx("record format", "undefined")#idx("empty line")
In text one record is one line for the fixed record formats and for the
variable record formats #cmd("V"), #cmd("VB") and #cmd("VBA"). For record
format #cmd("U") one block is one line. A record or block that holds nothing
but blanks is sent as an empty line. So a line that was empty when it was
stored (@ug-data-stor) comes back empty. With #cmd("site trailingblanks") the
blanks are kept, and such a line comes back as a single blank in a variable or undefined record
format. In a fixed format the line was padded to the record length when it was
stored, and comes back as blanks. Each NL byte
(#cmd("X'15'")) in a record or block ends a line, so a record with one NL byte
gives two lines, and an NL byte at the end of the record gives an extra empty
line.

#idx("load module")
Load modules have record format #cmd("U") and belong in binary. Text
translates the bytes as text, and a #cmd("X'00'") byte ends the line at that
byte.

The error replies are:

- *The data set or member does not exist:*
  #cmd("550 Data set NAME not found") for a data set and
  #cmd("550 Request nonexistent member NAME(MEMBER) to be sent.") for a
  member (the last lines of the session above).
- *The data connection breaks during the transfer:*
  #cmd("426 Transfer aborted: data connection lost after N bytes.")
- *The data set cannot be read to the end:* #cmd("451 Read error on data set after N bytes: transfer is incomplete.") The client holds a part of the data
  set.

== Uploading a Data Set <ug-data-stor>

#idx("STOR")
The client command #cmd("put") sends #cmd("STOR"). It writes the data FTPD
receives to a data set or to a member. A data set that does not exist is
created, with the attributes of the session (@ug-data-create). A data set or a
member that exists is replaced, unless its retention period has not expired
(@ug-data-retained). FTPD checks that the user may update the data set, sends
#cmd("125 Storing data set NAME"), reads the data and ends with
#cmd("250 Transfer completed successfully.") when every record has reached the
data set. The session stores a text file and a binary file, and fetches them
back:

#screen(raw(read("../ex/ug-data/d-upload.txt"), block: true))

The data set #cmd("REPORT") has the record format that the administrator set
as the default: #cmd("FB"), record length 80, block size 3120. The text file
came back unchanged (the #cmd("diff") says #cmd("identical")), because a text
transfer drops the blanks that the record was padded with.

The binary file has 300 bytes and came back with 320. FTPD cuts the bytes into
records of the record length, and it pads the last short record with binary
zeros, here 20 of them. A binary upload that is not a multiple of the record
length therefore does not come back the same. Describe a data set whose records
have the right length before the upload (@ug-data-create).

How the records are made depends on the transfer type:

- *Text:* each line becomes one record, translated from ASCII to EBCDIC (code
  page 037). For a fixed record format the record is padded with blanks. A
  line that does not fit into the record is an error (@ug-data-long). An empty
  line in a data set of a variable or undefined record format is stored as a
  record of one blank, because a record of no bytes cannot be written.
- *Binary and EBCDIC:* the bytes are cut into records of the record length, or
  of the block size for record format U. A last short record is padded with
  binary zeros (not for record format U).

#idx("allocation")
A data set that #cmd("put") creates is a sequential data set. FTPD allocates it
with the record format, record length, block size and space of the session,
writes it, and at the end releases the space that was not used (the data set
is opened with the #cmd("RLSE") option). So a generous primary quantity costs
nothing once the transfer has finished. A data set that existed is not
shortened.

=== Describing the New Data Set <ug-data-create>

#idx("SITE", "new data set")
A client does not tell FTPD the shape of the data it sends. FTPD takes it from
the session: from the default values of the administrator, and from the
#cmd("site") command (@ug-site). The client command #cmd("site") sends
#cmd("SITE"):

#screen(raw(read("../ex/ug-data/d-create.txt"), block: true))

The session creates two data sets. #cmd("WIDE") is fixed with records of 132
bytes. The first #cmd("site") asked for a block size of 3120, which is not a
multiple of 132. FTPD rounded it down to 3036, which the later #cmd("dir")
shows. It said so in the first line of its reply, which is longer than the
usual one-line #cmd("200"). #cmd("VARIABLE") is variable blocked. The command
#cmd("site recfm") with no value takes #cmd("RECFM") back to what it was at
the start of the session. The last command is a word that #cmd("SITE") does
not know. FTPD still answers #cmd("200"), and only the first line says what is
wrong, so read the whole reply (@ug-site-form).

The attributes stay for the rest of the session, so the next #cmd("put")
creates a data set with them. FTPD does not check #cmd("RECFM") and
#cmd("LRECL") itself. It passes them to the allocation, and a combination that
the system rejects shows when #cmd("put") creates the data set.

=== When a Line Is Too Long <ug-data-long>

#idx("TRUNCATE")#idx("line", "too long")#idx("451", "line too long")
In text one line is one record. A record holds a fixed number of data bytes:

- *Fixed record formats (F):* the record length.
- *Variable record formats (V):* the record length minus 4, because the record
  length counts the 4 bytes of the record descriptor word, which the system
  adds. A data set with #cmd("VB") and #cmd("LRECL=14") takes 10 bytes.
- *Record format U:* the block size.

A line of exactly this many bytes fits. The count is of the bytes that the
client sent, without the line end and without any CR. A data set that reports
no length takes lines of up to 32767 bytes. A variable data set with a record
length of 4 or less has no data bytes, so every line that is not empty is too
long.

A longer line makes the upload fail. In the session the data set has records of
10 bytes, and the file has a line that is longer:

#screen(raw(read("../ex/ug-data/d-long.txt"), block: true))

FTPD stopped the transfer at that line and answered #cmd("451"). The number in
the reply is the count of data bytes. When this #cmd("put") created the data
set, FTPD removes it again, as the empty #cmd("dir") shows. A data set that
existed is left as the failed upload left it, incomplete. The client holds the
only complete copy, so keep it.

#cmd("site truncate") changes this for the rest of the session. FTPD then cuts
the long line to the data bytes of the record, throws the rest away, goes on,
and ends the transfer with #cmd("250 Transfer completed successfully (data was
truncated)."), as the second #cmd("put") shows. The data set holds the first 10
bytes of each long line. Each use of #cmd("site truncate") switches again, so
the session ends with the switch off, as it began. A transfer in which no line
was cut ends with the plain #cmd("250 Transfer completed successfully."), also
with #cmd("truncate") on. FTPD writes a warning to the log for a transfer that
cut lines. The rule applies to #cmd("put") and #cmd("append") of the MVS view
in text. In binary the bytes are cut into records by length, so no line can be
too long.

=== When the Space Is Not Enough <ug-data-space>

#idx("space", "out of")#idx("552")
A client does not tell FTPD how large the data is, so the allocation is a
guess. When it is too small, the write fails. The session uses a file of about
300000 bytes and one track of space:

#screen(raw(read("../ex/ug-data/d-space.txt"), block: true))

FTPD answers #cmd("552"), and the new data set is gone, as the empty #cmd("dir")
shows. The second #cmd("site") asks for two cylinders, and the same #cmd("put")
works. The data set then uses 7 tracks: FTPD released the rest of the two
cylinders at the end of the transfer. When the system ends the write with a
space abend (B37, D37 or E37) instead of reporting the error, the reply is the
same with the abend code in it, as in #cmd("out of space on the data set
(ABEND SB37). Allocate more space ...").

The code is #cmd("552") and not #cmd("452") on purpose. A code in the 4xx range
tells a client to try again, and the same upload into the same space fails
the same way. When this #cmd("put") created the data set, FTPD removes it again,
so a retry does not find a half-filled data set. A data set that existed is left
as the failed write left it. The client holds the only complete copy, so keep
it. Any other error while writing is answered with #cmd("451 Write error on
data set after N bytes received: the data set is incomplete."), and the same
rule about the new data set applies.

Raise the space for the session with #cmd("site primary"),
#cmd("site secondary") and #cmd("site cylinders") (@ug-site), or ask the
administrator to raise #cmd("DEFPRIMARY") for everyone.

== Partitioned Data Sets and Members <ug-data-pds>

#idx("partitioned data set")#idx("member")
A partitioned data set holds members. A client creates the partitioned data
set with #cmd("mkdir") (#cmd("MKD")), and it can read, write, rename and delete
the members. The session starts with an attempt to store a member into a
partitioned data set that does not exist yet:

#screen(raw(read("../ex/ug-data/d-pds.txt"), block: true))

There are two ways to name a member:

- *With the working directory in the partitioned data set.* After
  #cmd("cd MYLIB"), every argument without quotes and without a parenthesis is
  a member name. FTPD makes it valid: a path before the last slash is dropped,
  and so is everything from the first period on (a file extension that the
  client adds). The rest is folded to upper case and cut to eight characters.
  So #cmd("put hello.c") and #cmd("put HELLO") both store the member
  #cmd("HELLO"). FTPD then checks the member name like the other names do
  (@ug-data-parens).
- *With the data set and the member in the name*, as in
  #cmd("'IBMUSER.MYLIB(BETA)'"), as in the second #cmd("get") above. FTPD makes
  the member name valid in the same way.

The session also shows that #cmd("rename") (#cmd("RNFR") and #cmd("RNTO")) and
#cmd("delete") (#cmd("DELE")) work on a member of the working directory. The
listing after them has only the member #cmd("GAMMA"), and the #cmd("get") of the
deleted member #cmd("BETA") fails. The members that FTPD stores carry no ISPF
statistics, so the listing shows the names only.

A member can only be stored into a partitioned data set that exists. The
first #cmd("put") shows the reply for a name with a parenthesis, and #cmd("mkdir")
creates the partitioned data set (@ug-data-manage). The attributes of the
session apply (@ug-data-create), and #cmd("site directory=n") sets the number
of directory blocks.

=== Parentheses in Names <ug-data-parens>

#idx("parenthesis")#idx("member", "name form")#idx("501", "parenthesis")
FTPD reads a name to its end, the part in parentheses included. A member is
written #cmd("DSN(MEMBER)") and stands at the end of the name. These rules
decide whether FTPD takes it:

- *The member is not empty.* In a command that names one member, it holds the
  characters that a qualifier holds (@ug-data-names): letters, digits,
  #cmd("@"), #cmd("#"), #cmd("$") and #cmd("-").
- *Nothing follows the closing parenthesis*, and there is one member only.
- *An extension is allowed.* #cmd("'DSN(alpha.txt)'") is the member
  #cmd("ALPHA"): FTPD cuts the extension off, as it does for a name in a
  partitioned data set, but it checks the characters of the extension too. In a
  command that names one member, a member that is empty once the extension is
  cut off, such as #cmd("'DSN(.X)'"), is refused.
- *Lower case is folded to upper case*, as it is in the rest of the name.
- *A pattern character is allowed in a listing only*, as in
  #cmd("nlist 'DSN(AL*)'") (@ug-data-list).

A name that breaks a rule is answered with
#cmd("501 Invalid data set name ... Use MVS Dsname conventions."), and FTPD does
nothing else. That holds for every command that names one member: #cmd("get"),
#cmd("put"), #cmd("delete") and #cmd("rename"). #cmd("mkdir") and #cmd("rmdir")
take no member at all and answer #cmd("501") for a well-formed one too. A
listing checks only the form of the name: a member that is not closed, or text
after the closing parenthesis, is refused. The text between the parentheses is
a filter that FTPD does not check character by character, so a character that
no member name can hold finds no member, and the reply is
#cmd("550 No data sets found.").
The session shows the cases that matter, from the working directory
#cmd("IBMUSER.") first and then from inside the partitioned data set:

#screen(raw(read("../ex/ug-data/d-parens.txt"), block: true))

Notes on the session:

- *#cmd("cd") takes no member.* Both #cmd("cd 'IBMUSER.MYLIB(ALPHA)'") and the
  one with the missing parenthesis are answered with #cmd("501"), and the
  second #cmd("pwd") shows the working directory unchanged. Inside the
  partitioned data set, #cmd("cd ALPHA") without quotes is a member form too,
  and is refused in the same way. #cmd("cd 'IBMUSER.MYLIB'") with quotes and
  #cmd("cd ..") work there as before.
- *#cmd("mkdir") and #cmd("rmdir") take no member either.* They act on a whole
  data set. A name with a member, such as
  #cmd("rmdir 'IBMUSER.MYLIB(ALPHA)'"), is answered with #cmd("501") before
  FTPD asks the security system, the catalog or IDCAMS anything, and the member
  stays. Inside the partitioned data set, #cmd("rmdir ALPHA") without quotes is
  a member form and is refused in the same way. To delete a member, use
  #cmd("delete") (@ug-data-manage).
- *A listing of a member that is not closed is refused.* The same holds for
  #cmd("'DSN(A)(B)'") and #cmd("'DSN(A)X'"). A pattern in the parentheses, as in
  #cmd("'IBMUSER.MYLIB(AL*)'"), lists the members that fit. #cmd("nlist 'DSN()'") and
  #cmd("nlist 'DSN(*)'") list every member. This applies when the
  working directory is not a partitioned data set. Inside one, a listing
  argument is a member filter, and FTPD does not read a name in it: the
  malformed arguments find no member and are answered with
  #cmd("550 No data sets found.").
- *A member with an extension still works.* The #cmd("get") of
  #cmd("'IBMUSER.MYLIB(alpha.txt)'") sends the member #cmd("ALPHA").
- *A name that no member can have is refused at once.* Inside the partitioned
  data set, #cmd("delete AL_PHA") is a member form, and the underscore cannot
  be in a member name. FTPD answers with the member name it built,
  #cmd("IBMUSER.MYLIB(AL_PHA)"). FTPD 1.3.1 and earlier did not check such a
  name, and it went on to the security check and to the system as it was.

#note[*Empty members and #cmd("DELE").* FTPD 1.3.1 and earlier cut the
extension off first and did not look at what was left. With
#cmd("delete 'IBMUSER.MYLIB(.X)'") no member name was left, FTPD took the name
for the data set itself, and deleted the whole partitioned data set. Later
versions answer #cmd("501"). A script that built member names from file names
and deleted them has been exposed to this.]

FTPD 1.3.1 and earlier read a name only up to the first parenthesis and did
not look at the rest. #cmd("cd 'IBMUSER.MYLIB(ALPHA)'") was answered with
#cmd("250") and kept the parenthesis in the working directory prefix.
#cmd("rmdir 'IBMUSER.MYLIB(ALPHA)'") deleted the member. Only arguments that
FTPD handled wrongly before change their answer. The _FTPD Reference_,
"Member Names", has the forms in a table.

=== When the Directory Is Full <ug-data-dirfull>

#idx("directory", "full")#idx("partitioned data set", "directory full")#idx("552")
A partitioned data set has a fixed number of directory blocks. The number is
set when #cmd("mkdir") creates it, and nothing can raise it later. The session
creates one with a single directory block and stores members until the
directory is full. It is shortened: the transfers of #cmd("m02.txt") to
#cmd("m19.txt") are left out where the marker #cmd("[... ...]") stands, and
each of them ends with #cmd("250"):

#screen(raw(read("../ex/ug-data/d-dirfull.txt"), block: true))

With these member names 20 members fit into the block. For the 21st the system
ends the close of the data set with the ABEND SB14, and FTPD answers
#cmd("552"). The text says "most likely" because FTPD gets the ABEND code SB14
but not the reason code that goes with it. Every SB14 during a #cmd("put") or
#cmd("append") gets this reply. The reason code is in the message
#cmd("IEC217I") on the console, which shows #cmd("B14-0C") for a full
directory. The console shows #cmd("FTPD070E ABEND SB14 RECOVERED CMD=STOR") as
well (@ug-ops-abend).

The code is #cmd("552") for the reason given in @ug-data-space: a retry meets
the same full directory. The member that failed is not in the data set (the
#cmd("dir") ends with #cmd("M20")), and the started task goes on serving.

The directory of the partitioned data set cannot be extended. To get more
room, the members have to move into a new partitioned data set that has more
directory blocks:

+ Find out how many directory blocks the full data set has. FTPD does not show
  the number, and #cmd("dir") does not either. Ask the person who looks after
  the data set or who created it. If you created it with #cmd("mkdir"), you
  know the value of #cmd("site directory") you used, or #cmd("DEFDIRECTORY") of
  the server if you set none (@ug-site).
+ Choose a name for the new partitioned data set that is free. #cmd("mkdir")
  answers #cmd("550") for a name that exists, so either give the new data set
  another name than the full one, or rename the full one first with
  #cmd("rename") (@ug-data-manage) and create the new one under the old name.
+ Set a number higher than that of the full data set with
  #cmd("site directory=n"), and create the new data set with #cmd("mkdir").
  #cmd("site directory") acts on #cmd("mkdir") only. A #cmd("put") that creates
  a data set makes a sequential one.
+ Copy the members one by one: #cmd("get") each member from the old data set
  to the client, and #cmd("put") it into the new one (@ug-data-mget shows how to
  do it for many). FTPD has no command that copies on the server. On the host,
  the utility IEBCOPY does the same work.
+ Delete the old data set when it is no longer needed.

A transfer carries the records of a member and nothing else. What the
directory entry holds besides them, such as the ISPF statistics that
#cmd("dir") shows, is not carried, and an alias is not recreated. A member
that was fetched and stored under the name of an alias is a member of its own.

A partitioned data set that FTPD did not create is created again with the tool
that created it, with more directory blocks.

== Moving Several Data Sets at Once <ug-data-mget>

#idx("MGET")#idx("MPUT")
The client commands #cmd("mput") and #cmd("mget") move many files with one
command. They are loops of the client, and FTPD sees single commands:

- #cmd("mput *.txt") expands the pattern on the local disk and sends one
  #cmd("STOR") for each file.
- #cmd("mget T*") sends #cmd("NLST T*") to get the names, and then one
  #cmd("RETR") for each name (@ug-data-list shows what the pattern means).
- #cmd("mdelete O*") sends #cmd("NLST") in the same way, and then one
  #cmd("DELE") for each name.

By default #cmd("tnftp") asks before every file. #cmd("prompt") switches the
question off and on. The session works inside the partitioned data set from the
previous section. It stores the files #cmd("one.txt"), #cmd("two.txt") and
#cmd("three.txt") with the questions on, then again with the questions off,
and then fetches and deletes by pattern:

#screen(raw(read("../ex/ug-data/d-mget.txt"), block: true))

The members were named from the local file names: the name #cmd("one.txt")
became the member #cmd("ONE") (@ug-data-pds). #cmd("mget T*") created the local
files #cmd("THREE") and #cmd("TWO") in the local directory. The client names
the local file as the server's name for the member. The answers #cmd("y") and
#cmd("n") in the first #cmd("mput") are the client's questions
(#cmd("[anpqy?]")); FTPD never sees them.

== Appending, Renaming and Deleting <ug-data-manage>

#idx("APPE")#idx("MKD")#idx("DELE")#idx("RMD")#idx("RNFR")
These client commands work on data sets and members:

#deflist(width: 1.4in,
  [#cmd("append") (#cmd("APPE"))], [adds the data it receives to the end of a
    sequential data set.],
  [#cmd("mkdir") (#cmd("MKD"))], [creates a partitioned data set with the
    attributes of the session. #cmd("site directory") sets the number of
    directory blocks (the value of #cmd("DEFDIRECTORY"), 10 if the
    administrator did not change it), and #cmd("site retpd") the retention
    period. A name that exists is answered with #cmd("550 NAME already
    exists.").],
  [#cmd("delete") (#cmd("DELE"))], [deletes a data set, or a member when the
    name has one.],
  [#cmd("rmdir") (#cmd("RMD"))], [deletes a data set.],
  [#cmd("rename") (#cmd("RNFR"), #cmd("RNTO"))], [renames a data set or a
    member.],
)

The session appends to a data set, appends to a name that does not exist,
tries to append to a partitioned data set, renames a data set, and deletes:

#screen(raw(read("../ex/ug-data/d-manage.txt"), block: true))

#cmd("append") sends #cmd("125 Appending to data set NAME") for a sequential
data set that exists, and the data goes to the end: the file #cmd("b.txt") is
now the end of #cmd("REPORT"). A data set or a member that does not exist is
created, as #cmd("put") creates it, and the data is its first content
(#cmd("NEWAPP")). A partitioned data set named without a member is refused
with #cmd("550 APPE needs a sequential data set or a new member"). A member
that exists is refused as well, and the member is not touched:

#screen(```
550 APPE cannot append to the existing member IBMUSER.SRC(HELLO): appending to a PDS member is not supported, the member is unchanged. Use STOR to replace it.
```)

FTPD makes these checks before it allocates or opens anything. A client that
wants to extend a member has to fetch it, join the parts and store the whole.
The UFS view does not append (@ug-ufs).

A rename needs both protocol commands, #cmd("RNFR") with the old name and
#cmd("RNTO") with the new name. The client command #cmd("rename") sends them
one after the other. #cmd("RNTO") must be the very next command: FTPD forgets
the old name when any other command comes between. A data set is renamed to a
data set that does not exist yet. A member is renamed within its partitioned
data set. FTPD does not turn a member into a data set or the other way round,
and it does not move a member to another partitioned data set.

#cmd("delete") with an unquoted name deletes the data set below the working
directory. In a partitioned data set that is the working directory, an unquoted
name is a member, so quote the name to delete a data set. The reply for a data set that is not there is
#cmd("550 DELE fails: NAME does not exist."), as the second #cmd("delete
NEWAPP") shows, after the first one, with the full name in quotes, had deleted
it. #cmd("rmdir MYLIB") deleted the partitioned data set with all its members.
#cmd("rmdir") and #cmd("mkdir") take the name of a data set, never a member
(@ug-data-parens). These commands need the authority to alter the data set (@ug-security-dsn).
The reply to a #cmd("delete"), a #cmd("rmdir") or the rename of a whole data set
that fails names the reason and the return code of IDCAMS.

=== A Data Set Under Retention <ug-data-retained>

#idx("retention period")#idx("IEC507D")#idx("550", "retained data set")
A data set that was allocated with a retention period must not be overwritten
before the period is over. #cmd("site retpd=n") sets the period in days for
the data sets that #cmd("put") and #cmd("mkdir") create. #cmd("put") and
#cmd("append") of an existing data set check the expiration date before they
open it. When it has not come, FTPD refuses and names the date:

#screen(raw(read("../ex/ug-data/d-retain.txt"), block: true))

The first #cmd("put") created #cmd("KEEPME") with one day of retention. The
second, and the #cmd("append"), got the #cmd("550"), and the data set was not
touched. #cmd("delete") does not check the date, and neither does
#cmd("rmdir"): to replace a retained data set, delete it, or store under a new
name. The bare #cmd("site retpd") switches the retention off, and the data set
that the next #cmd("put") creates can be stored into again and again.

- A data set expires on its expiration date. On that day #cmd("put") and
  #cmd("append") work again.
- The expiration date is in the data set, so every session is refused, not
  only the one that used #cmd("site retpd").
- A data set with the expiration date 99365 or 99366 never expires. The reply
  for it is #cmd("550 NAME has a permanent retention period and is not opened for
  writing."). A data set with the expiration date 99000 is no calendar date,
  but it is retained all the same, with its own reply, #cmd("550 NAME has the
  expiration date 99000 and is not opened for writing."). For #cmd("append") the
  last word of the replies is #cmd("appending").
- A member is stored into a partitioned data set that exists, and its label
  decides. A partitioned data set that #cmd("mkdir") created under
  #cmd("site retpd") takes no member, new or existing, until it expires. To get
  a partitioned data set that takes members, set #cmd("site retpd") back to
  nothing before #cmd("mkdir").

FTPD 1.2.0 and earlier do not check. There the system asks the operator with
message #cmd("IEC507D") when it opens a retained data set for output, and the
session waits for the reply. The _FTPD Reference_ has the texts and the order
of the checks, in "RETPD".

== Transferring with curl <ug-data-curl>

#idx("curl")
#cmd("curl") moves a single file with one command line. Its default is binary,
so say #cmd("-B") for text, in the upload and in the download. The option
#cmd("-s") hides the progress meter, and a successful upload prints nothing.
The #cmd("password") in the session is a placeholder for the password of the
user ID. The session stores the text file #cmd("report.txt") as
#cmd("CURLTEXT"), reads it back as text, and reads the same data set as binary.
The last two commands store the file as binary, and read it as text:

#screen(raw(read("../ex/ug-first/curl.txt"), block: true))

The text round trip gives the four lines back, with the empty one. The binary
read of the same data set shows its bytes in hex: #cmd("c6 89 99 a2 a3") is
#cmd("First") in EBCDIC. The last two commands are the mistake to avoid. The upload without #cmd("-B") stored the
bytes of the file as they are, which are ASCII. A text download translates
them as if they were EBCDIC, and the result is nothing that can be read. Use the
same type in both directions.
