#import "../bookmaster/bookmaster.typ": *

= Working with Files in UFS <ug-ufs>

#idx("UFS")#idx("UFSD")#idx("HFS")
UFS is the Unix-like file system of the UFSD (ufsd) address space. FTPD shows
it to a client as a tree of directories and files, as an FTP server on Unix
does. FTPD talks to UFSD for this. UFSD is optional: without it, FTPD works
with data sets and JES. The first UFS command of a session then gets
#cmd("550 UFS service not available"), and the session stays in the data set
view.

The sessions in this chapter use the same client as the rest of the book
(@ug-first-read). The file #cmd("report.txt") is a text file of the local
directory.

== Switching to UFS and Back <ug-ufs-switch>

#idx("CWD", "UFS")
The working directory decides the view. #cmd("cd") (#cmd("CWD")) with a name
that starts with a slash switches to UFS, and a name in single quotes switches
back to the data set view (@ug-data-cwd). One session shows all the tasks of this
chapter. It switches, makes a directory, writes a file into it, lists the
directory, asks for the size and the time of the file, fetches the file and
compares it with the original, then tries to rename and to append, deletes the
file and the directory, asks for files that are not there, and returns to the
data set view:

#screen(raw(read("../ex/ug-ufs/f-ufs.txt"), block: true))

The first reply is #cmd("250 HFS directory /u/ibmuser is the current working
directory"), and #cmd("pwd") then shows the path without quotes, which is the
sign of the UFS view. In the UFS view:

- A #cmd("cd") to a name without a leading slash moves relative to the current
  directory, and #cmd("cdup") (#cmd("CDUP")) goes to the parent. The path may
  contain #cmd(".") and #cmd(".."), and double slashes. FTPD resolves them. A
  path cannot go above the root: #cmd("..") at the root stays at the root. A
  path that is too long gets #cmd("550 Invalid path").
- A #cmd("cd") to a directory that does not exist gets
  #cmd("550 File or directory not found"), and the working directory stays.
- #cmd("cd 'IBMUSER.'") with the quotes returns to the data set view. The reply
  is the one of the data set view, and #cmd("pwd") shows the prefix again.

FTPD tells UFSD the user ID and the group of the session, and UFSD applies the
owners and permissions of the file system. A refusal is
#cmd("550 Permission denied").

== Listing, Reading and Writing Files <ug-ufs-files>

#idx("LIST", "UFS")
#cmd("dir") and #cmd("ls") (#cmd("LIST"), and #cmd("NLST") with #cmd("nlist"))
show the working directory. The first reply is
#cmd("150 Opening data connection for file list"). #cmd("LIST") sends every
entry as a line in the Unix long format, and #cmd("NLST") sends the names only.
The long format is: permissions, link count,
owner, group, size, date and name. The date is always month, day and time,
without a year. The entries #cmd(".") and #cmd("..") are not listed. In the
session the directory #cmd("/u/ibmuser") was empty, so #cmd("dir") printed
nothing between the #cmd("150") and the #cmd("226"). In #cmd("tnftp"), #cmd("ls")
and #cmd("dir") both send #cmd("LIST"), so both show the long format (see
"Client Commands and Protocol Commands" in the _FTPD Reference_).

#cmd("put") and #cmd("get") (#cmd("STOR") and #cmd("RETR")) answer
#cmd("150 Opening data connection for") and the path of the file, and
#cmd("226 Transfer complete") at the end. #cmd("put") replaces a file that
exists. The file that went up and the file that came back are the same in the
session, shown by the #cmd("diff") of the client. A #cmd("get") that cannot read
the file to the end does not get the #cmd("226")
(@ug-ufs-read-errors).

#cmd("size") and #cmd("modtime") (#cmd("SIZE") and #cmd("MDTM")) work in the UFS
view: the reply is #cmd("213") with the size in bytes or the time of the last
change. The client prints the time in its own form. In the data set view both
commands get #cmd("502") (@ug-data-manage).

#idx("TYPE", "UFS")
In text mode (#cmd("ascii")) FTPD converts the content between ASCII and
EBCDIC, as it does for data sets. For UFS files the EBCDIC code page is
IBM-1047. For data sets it is IBM-037 (@ug-data-retr). A few characters differ
between the two, for example the square brackets. Use binary mode
(#cmd("binary")) for a file that is not text. FTPD then sends the bytes as they
are.

== Making, Renaming and Deleting <ug-ufs-manage>

The second half of the session shows the commands that change the directory
tree:

- #cmd("mkdir") (#cmd("MKD")) answers #cmd("257 \"/u/ibmuser/work\" directory
  created") with the full path.
- #cmd("delete") (#cmd("DELE")) answers #cmd("250 File deleted"), and a second
  #cmd("delete") of the same file gets #cmd("550 File or directory not found").
- #cmd("rmdir") (#cmd("RMD")) answers #cmd("250 Directory removed"). It refuses
  a directory that is not empty.
- #cmd("rename") sends #cmd("RNFR") and #cmd("RNTO"). The first one answers
  #cmd("350 File exists, ready for destination"), and the second one answers
  #cmd("502 Rename not supported for UFS"). A file cannot be renamed in UFS with
  FTP. Copy it under the new name and delete the old one.
- #cmd("append") (#cmd("APPE")) answers #cmd("502 APPE not supported for UFS").
  Append works for data sets only (@ug-data-manage).

== When a File Cannot Be Written <ug-ufs-errors>

#idx("552", "UFS")#idx("inode")
The replies to a failed #cmd("put") depend on the cause:

#tab(caption: [Errors of STOR in the UFS view])[
  #table(columns: (1.6in, 1fr),
    [Reply], [Cause],
    [#cmd("552 No inodes available")],
      [The file system has no free inode and cannot create the file.
      #cmd("mkdir") gets the same reply. A retry gets the same answer. Before
      FTPD 1.3.0 the reply was #cmd("452"), which invites a client to try
      again.],
    [#cmd("552 Requested file action aborted: no space for the file. ...")],
      [The file system is full during the write, or the file has reached the
      largest size that UFS supports. The text ends with "The file on the
      server is incomplete."],
    [#cmd("452 No space left on device")],
      [UFSD reports a lack of space when the file is created or for a command
      other than #cmd("STOR"), for example #cmd("MKD"). A retry later can
      work. A lack of space during the write is the #cmd("552") above.],
    [#cmd("451 Write failed on the server after n bytes received: the file is incomplete.")],
      [The write failed and UFSD gave no reason, or the request never reached
      UFSD. Since FTPD 1.3.1 the second case is this reply; it is never a
      success.],
    [#cmd("426 Connection closed; transfer aborted after n bytes. The file on the server is incomplete.")],
      [The data connection broke.],
    [#cmd("553 Path name too long")], [The name is longer than UFS allows.],
  )
]

A file in UFS can have about 4 MB at most. This is the size that UFS reaches
with 16 direct blocks and 1024 indirect blocks of 4096 bytes: 4.06 MB. A
larger file gets the 552 reply for the write even on an empty file system.

After a failed #cmd("put") the incomplete file stays in the directory. FTPD
cannot tell a file that the command has created from one that it has replaced,
and the replaced content is gone in both cases. Delete the file or send it
again.

Other errors of UFSD map to these replies: #cmd("550") for a missing file or
directory, a file that exists, a name that is not a directory or is one, a
directory that is not empty, a file system that is read-only, and a refused
permission. #cmd("451") is the reply for an I/O error and for any other code.

== When a File Cannot Be Read <ug-ufs-read-errors>

#idx("451", "UFS")#idx("426", "UFS")#idx("RETR", "UFS")
FTPD sends #cmd("226 Transfer complete") for a #cmd("get") only when it has
sent the file to the end. Two things can stop a #cmd("get") after the data has
begun to flow. In both cases FTPD closes the data connection, and the reply
says how many bytes FTPD had sent:

#tab(caption: [Errors of RETR in the UFS view])[
  #table(columns: (1.6in, 1fr),
    [Reply], [Cause],
    [#cmd("451 Read error on the server after n bytes: transfer is incomplete.")],
      [FTPD could not read the file to the end. The read failed in UFSD, or
      the request never reached UFSD. The client holds the first #cmd("n")
      bytes of the file.],
    [#cmd("426 Connection closed; transfer aborted after n bytes.")],
      [A send on the data connection failed, so the client did not receive the
      rest. When the read failed as well, this reply is the one FTPD sends.],
  )
]

Before FTPD 1.3.3 both cases were answered with #cmd("226 Transfer complete"),
and a client kept a part of the file as if it were the whole.
A client that checks the reply code sees the failure now.

A transfer that fails is not counted in the number of transfers that FTPD
writes to the log at the end of the session (@ug-ops-log). FTPD writes one
line at the level ERROR for the failure to the log, and none to the console:

- #cmd("[ERROR] RETR: read failed on PATH after N bytes sent (rc=RC)") goes
  with #cmd("451"). #cmd("RC") is the return code that UFSD gave for the
  failing request, or #cmd("-1") when the request never reached UFSD.
- #cmd("[ERROR] RETR: data connection failed on PATH after N bytes") goes with
  #cmd("426").

#cmd("PATH") is the path that FTPD resolved from the name that the client
sent. Fetch the file again after the cause is removed.
