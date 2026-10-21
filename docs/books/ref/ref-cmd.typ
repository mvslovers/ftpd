#import "../bookmaster/bookmaster.typ": *

= FTP Commands <ref-cmd>

#idx("commands", "FTP")
This chapter lists every FTP command that FTPD (ftpd) knows, with the replies
that the command sends. The reply codes are in @ref-reply. The
chapter says in which view a command works: the MVS view (data sets), the UFS
view (files) or the JES view (jobs). The _FTPD User's Guide_ explains the
views.

A command word is not case sensitive. FTPD answers a word it does not know
with #cmd("500 unknown command WORD"), where WORD is the word as it was sent.
That includes commands that other servers have, for example #cmd("REST"),
#cmd("STOU") and #cmd("ALLO").

== Before and After the Login <ref-cmd-auth>

Until a user is logged in, FTPD accepts only these commands: #cmd("USER"),
#cmd("PASS"), #cmd("QUIT"), #cmd("SYST"), #cmd("FEAT"), #cmd("HELP"),
#cmd("NOOP"), #cmd("STAT") and #cmd("AUTH"). With #cmd("SSLPROXY=YES") also
#cmd("PBSZ") and #cmd("PROT"). All others get #cmd("530 Not logged in.").
After the login, #cmd("USER") and #cmd("PASS") are not commands any more, and
FTPD answers them with the #cmd("500") reply. A client that wants to change
the user opens a new connection.

== Command Summary <ref-cmd-sum>

#tab(caption: [FTP commands])[
  #table(columns: (1.15in, 1fr),
    [Command], [Reply],
    [#cmd("USER name")], [#cmd("331 Send password please."). The user ID is
      kept as it is; the check comes with #cmd("PASS").],
    [#cmd("PASS word")], [#cmd("230") on success; #cmd("530") otherwise
      (@ref-reply-530). Three failures close the connection.],
    [#cmd("QUIT")], [#cmd("221 Quit command received. Goodbye.") and the
      connection closes.],
    [#cmd("SYST")], [#cmd("215 MVS is the operating system of this server. FTP Server is running on MVS."). In the UFS view: #cmd("215 UNIX is the operating system of this server. FTP Server is running on MVS.").],
    [#cmd("FEAT")], [#cmd("211-Features supported"), then the lines
      #cmd("EPSV"), #cmd("SIZE"), #cmd("MDTM"), #cmd("SITE FILETYPE"),
      #cmd("SITE JES"), #cmd("UTF8"), and #cmd("PBSZ") and #cmd("PROT") with
      #cmd("SSLPROXY=YES"), then #cmd("211 End"). The list names #cmd("SIZE")
      and #cmd("MDTM"), although the MVS view answers both with #cmd("502").],
    [#cmd("HELP")], [#cmd("214-The following commands are recognized:")
      and #cmd("214 HELP command successful."). The reply has no list of
      commands between the two lines.],
    [#cmd("NOOP")], [#cmd("200 OK").],
    [#cmd("STAT")], [#cmd("211") and the product name and the version of the
      build, such as #cmd("211 MVS 3.8j FTPD Server 1.3.0"). There is no form of
      the command with a path.],
    [#cmd("AUTH mech")], [#cmd("504 Security mechanism not implemented.")
      FTPD has no TLS.],
    [#cmd("PBSZ n")], [#cmd("200 PBSZ=0"), with #cmd("SSLPROXY=YES") only.
      Otherwise #cmd("500").],
    [#cmd("PROT level")], [With #cmd("SSLPROXY=YES"): #cmd("P") or #cmd("C")
      gives #cmd("200 Data channel protection level set to P") (or
      #cmd("C")); another level gives #cmd("504 Protection level X not supported"); no level gives #cmd("501 PROT requires a protection level").
      Otherwise #cmd("500"). FTPD sends nothing encrypted.],
    [#cmd("OPTS ...")], [#cmd("202 UTF8 mode is always enabled.") for any
      argument.],
    [#cmd("TYPE t")], [@ref-cmd-type.],
    [#cmd("STRU s")], [@ref-cmd-type.],
    [#cmd("MODE m")], [#cmd("MODE S") gives #cmd("200 Data transfer mode is Stream"). Another mode gives #cmd("504 Only stream mode supported").],
    [#cmd("PWD"), #cmd("XPWD")], [#cmd("257"). The text depends on the view:
      @ref-cmd-cwd.],
    [#cmd("CWD path"), #cmd("XCWD path")], [@ref-cmd-cwd.],
    [#cmd("CDUP"), #cmd("XCUP")], [One level up, in the view that is active.],
    [#cmd("PORT h1,h2,h3,h4,p1,p2")], [#cmd("200 PORT command successful") or
      #cmd("501 Syntax error in PORT parameters"). After #cmd("EPSV ALL"):
      #cmd("501 PORT not allowed after EPSV ALL").],
    [#cmd("PASV")], [#cmd("227 Entering Passive Mode (h1,h2,h3,h4,p1,p2)"), or
      #cmd("425 Cannot open passive connection"). After #cmd("EPSV ALL"):
      #cmd("501 PASV not allowed after EPSV ALL"). The port is taken from
      #cmd("PASVPORTS") (@ref-prm).],
    [#cmd("EPSV")], [#cmd("229 Entering Extended Passive Mode (|||port|)"), or
      #cmd("425 Cannot open passive connection").],
    [#cmd("EPSV ALL")], [#cmd("200 EPSV ALL command successful."). From then on
      the session refuses #cmd("PORT") and #cmd("PASV").],
    [#cmd("EPRT")], [#cmd("522 Network protocol not supported, use (1)").
      FTPD has no IPv6.],
    [#cmd("ABOR")], [Closes the data connection of the session and sends
      #cmd("226 Abort successful").],
    [#cmd("LIST [arg]"), #cmd("NLST [arg]")], [@ref-cmd-list.],
    [#cmd("RETR name")], [@ref-cmd-xfer.],
    [#cmd("STOR name")], [@ref-cmd-xfer.],
    [#cmd("APPE name")], [@ref-cmd-xfer.],
    [#cmd("DELE name")], [@ref-cmd-file.],
    [#cmd("MKD name"), #cmd("XMKD name")], [@ref-cmd-file.],
    [#cmd("RMD name"), #cmd("XRMD name")], [@ref-cmd-file.],
    [#cmd("RNFR name"), #cmd("RNTO name")], [@ref-cmd-file.],
    [#cmd("SIZE name")], [UFS view: #cmd("213") and the size in bytes. MVS
      view: #cmd("502 SIZE not implemented for MVS data sets").],
    [#cmd("MDTM name")], [UFS view: #cmd("213") and the time as
      #cmd("YYYYMMDDHHMMSS"). MVS view: #cmd("502 MDTM not implemented for MVS").],
    [#cmd("SITE ...")], [@ref-site.],
  )
]

== TYPE and STRU <ref-cmd-type>

#idx("TYPE")#idx("STRU")
#tab(caption: [Replies to TYPE and STRU])[
  #table(columns: (1.5in, 1fr),
    [Command], [Reply],
    [#cmd("TYPE A")], [#cmd("200 Representation type is Ascii NonPrint").],
    [#cmd("TYPE I")], [#cmd("200 Representation type is Image").],
    [#cmd("TYPE E"), #cmd("TYPE E N")], [#cmd("200 Representation type is Ebcdic NonPrint").],
    [#cmd("TYPE E x")], [For x other than #cmd("N"): #cmd("504-TYPE has unsupported format x") and #cmd("504 Type remains ..."), where the type
      is the one that was set before.],
    [#cmd("TYPE L 8")], [#cmd("200 Local byte 8, representation type is Image"). The type is binary.],
    [#cmd("TYPE L n"), other types], [#cmd("501-unknown type  ...") and
      #cmd("501 Type remains ...").],
    [#cmd("TYPE")], [#cmd("501-missing type parameter") and
      #cmd("501 Type remains ...").],
    [#cmd("STRU F")], [#cmd("250 Data structure is File").],
    [#cmd("STRU R")], [#cmd("250 Data structure is Record").],
    [#cmd("STRU P")], [#cmd("504-Page structure not implemented") and
      #cmd("504 Data structure remains ...").],
    [other], [#cmd("504-Unknown structure x") and #cmd("504 Data structure remains ...").],
  )
]

A session starts with #cmd("TYPE A") and #cmd("STRU F"). The type decides how
FTPD translates the data and cuts it into records. FTPD stores the structure and no
transfer reads it, so #cmd("STRU R") changes nothing.

== The Working Directory <ref-cmd-cwd>

#idx("CWD")#idx("PWD")
#tab(caption: [Replies to CWD and PWD])[
  #table(columns: (1.5in, 1fr),
    [Command and view], [Reply],
    [#cmd("PWD"), MVS view], [#cmd("257 \"'IBMUSER.'\" is working directory.")
      The name is in single quotes inside the double quotes.],
    [#cmd("PWD"), in a PDS], [#cmd("257 \"'SYS1.MACLIB'\" partitioned data set is working directory.")],
    [#cmd("PWD"), UFS view], [#cmd("257 \"/u/ibmuser\" is the HFS working directory.")],
    [#cmd("CWD /path")], [Switches to the UFS view, or stays in the view
      the session had if UFSD is not available (#cmd("550 UFS service not available")). The reply is #cmd("250 HFS directory /path is the current working directory").],
    [#cmd("CWD 'NAME'") in UFS], [Switches back to the MVS view.],
    [#cmd("CWD name") in MVS], [@ref-cmd-cwd-mvs.],
    [#cmd("CWD name") in UFS], [Relative to the UFS directory. The path is
      cleaned of #cmd("."), #cmd(".."), and double slashes. #cmd("..") at the
      root stays at the root. A path that is too long gets #cmd("550 Invalid path").],
  )
]

=== In the MVS View <ref-cmd-cwd-mvs>

The working directory is a data set name prefix. #cmd("CWD") sets or changes
it. The replies are:

- #cmd("250 \"IBMUSER.TEST.\" is the working directory name prefix.") for a
  name that is not a partitioned data set.
- #cmd("250 The working directory \"SYS1.MACLIB\" is a partitioned data set")
  for a partitioned data set.
- #cmd("501") for a name that is not valid (@ref-reply-501).

The rules for names are in the _FTPD User's Guide_, "Working with Data Sets".

== LIST and NLST <ref-cmd-list>

#idx("LIST")#idx("NLST")
#tab(caption: [LIST and NLST by view])[
  #table(columns: (1.1in, 1fr),
    [View], [Behaviour],
    [MVS], [One line for each data set, or for each member inside a partitioned
      data set. The argument is a filter. #cmd("*"), #cmd("**") and
      #cmd("%") are wildcards. Reply: #cmd("226 List completed successfully."); #cmd("550 No data sets found."); #cmd("451 Insufficient storage to list NAME."). #cmd("NLST") sends the names only.],
    [UFS], [#cmd("150 Opening data connection for file list") and
      #cmd("226 Transfer complete"). The lines are in the Unix long format.
      #cmd("NLST") sends the names only. The entries #cmd(".") and #cmd("..")
      are left out.],
    [JES], [The list of jobs, or with #cmd("JOBnnnnn") the list of the spool
      files of one job. #cmd("LIST") and #cmd("NLST") give the same list.
      @ref-cmd-jes.],
  )
]

== Transfer Commands <ref-cmd-xfer>

#idx("RETR")#idx("STOR")#idx("APPE")
#tab(caption: [Transfer commands by view])[
  #table(columns: (0.9in, 1fr),
    [Command], [Behaviour],
    [#cmd("RETR")], [MVS: #cmd("125 Sending data set NAME FIXrecfm 80") (the
      record format and length are those of the data set), the data, and
      #cmd("250 Transfer completed successfully."). UFS: #cmd("150 Opening data connection for PATH") and #cmd("226 Transfer complete"). JES: the output
      of a job (@ref-cmd-jes).],
    [#cmd("STOR")], [MVS: #cmd("125 Storing data set NAME"), the data, and
      #cmd("250 Transfer completed successfully."). Creates the data set or
      replaces it. A new data set gets the retention period of
      #cmd("SITE RETPD") (@ref-site-retpd). A data set whose retention period
      has not expired gets a #cmd("550") with the date
      (@ref-site-retained). UFS: #cmd("150 Opening data connection for PATH") and
      #cmd("226 Transfer complete"). A file that exists is replaced. JES:
      submits the data as a job.],
    [#cmd("APPE")], [MVS: appends to a sequential data set
      (#cmd("125 Appending to data set NAME")). Creates a data set or a member
      that does not exist. A member that exists, or a partitioned data set
      without a member, gets #cmd("550"), and so does a data set whose
      retention period has not expired (@ref-site-retained). UFS:
      #cmd("502 APPE not supported for UFS").],
  )
]

The data goes over a data connection that the client opened with
#cmd("PORT"), #cmd("PASV") or #cmd("EPSV") before the transfer command. When it
has not, or FTPD cannot reach the client, the reply is #cmd("425"). The
failures that end a transfer are in @ref-reply.

#note[*APPE changed in FTPD 1.3.0.* A server of FTPD 1.2.0 or earlier answers
#cmd("APPE") like #cmd("STOR") and replaces the data set. See the _FTPD User's
Guide_, "Working with Data Sets", for the rules.]

== DELE, MKD, RMD, RNFR and RNTO <ref-cmd-file>

#idx("DELE")#idx("MKD")#idx("RMD")#idx("RNFR")#idx("RNTO")
#tab(caption: [File management commands by view])[
  #table(columns: (0.9in, 1fr),
    [Command], [MVS view, UFS view, JES view],
    [#cmd("DELE")], [MVS: deletes a data set or a member (#cmd("250 NAME deleted.")). UFS:
      #cmd("250 File deleted"). JES: cancels the job and removes its output
      (@ref-cmd-jes).],
    [#cmd("MKD")], [MVS: creates a partitioned data set (#cmd("257 'NAME' created.")); #cmd("SITE DIRECTORY")
      sets the directory blocks, #cmd("DEFDIRECTORY") by default (10), and
      #cmd("SITE RETPD") the retention period (@ref-site-retpd). UFS: #cmd("257 \"PATH\" directory created").],
    [#cmd("RMD")], [MVS: deletes a data set. UFS: #cmd("250 Directory removed"); a directory that is not empty gets #cmd("550").],
    [#cmd("RNFR")], [MVS: #cmd("350 RNFR accepted. Please supply new name for RNTO."). UFS: #cmd("350 File exists, ready for destination").],
    [#cmd("RNTO")], [MVS: renames a data set or a member (#cmd("250 OLD renamed to NEW")). The command must
      follow #cmd("RNFR") at once. UFS: #cmd("502 Rename not supported for UFS").],
  )
]

== The JES View <ref-cmd-jes>

#idx("JES", "commands")
The JES view starts with #cmd("SITE FILETYPE=JES") and ends with
#cmd("SITE FILETYPE=SEQ"). In it, five commands go to JES2 and the others work
as before:

#tab(caption: [Commands in the JES view])[
  #table(columns: (1.25in, 1fr),
    [Command], [Behaviour],
    [#cmd("STOR any")], [Submits the data as a job. The file name is ignored.
      Reply: #cmd("125 Sending Job to JES internal reader FIXrecfm 80"), then
      #cmd("250-It is known to JES as JOBnnnnn") and
      #cmd("250 Transfer completed successfully."). FTPD adds #cmd("NOTIFY"),
      #cmd("USER") and #cmd("PASSWORD") to the JOB statement.],
    [#cmd("LIST"), #cmd("NLST")], [The list of jobs, filtered by the owner
      and the job name of #cmd("SITE"). At most 200 entries.],
    [#cmd("LIST JOBnnnnn")], [The spool files of one job. #cmd("550 No job found with ID JOBnnnnn") if there is none.],
    [#cmd("RETR JOBnnnnn")], [All output files of the job, each followed by
      the line #cmd(" !! END OF JES SPOOL FILE !!").],
    [#cmd("RETR JOBnnnnn.n")], [The output file number n, counted from 1. The
      input files of the job are not counted.],
    [#cmd("DELE JOBnnnnn")], [Cancels the job and removes its output:
      #cmd("250 Cancel successful").],
  )
]

The replies to errors, the format of the lists and the rules for JCL are in
the _FTPD User's Guide_, "The JES and UFS Views".
