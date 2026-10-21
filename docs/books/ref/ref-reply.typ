#import "../bookmaster/bookmaster.typ": *

= Reply Codes <ref-reply>

#idx("reply codes")
Every FTP reply starts with a three-digit code. The first digit says whether
the command worked (2), needs more (1 or 3), failed for now (4) or failed for
good (5). A reply of several lines has a hyphen after the code in all lines but
the last, as in #cmd("250-It is known to JES as JOB00042") followed by
#cmd("250 Transfer completed successfully."). A client must read to the last
line.

The text of a reply is meant for people. A client should act on the code. The
texts that this book quotes are the ones of FTPD 1.3.0.

== Codes That FTPD Sends <ref-reply-list>

#tab(caption: [Reply codes of FTPD])[
  #table(columns: (0.55in, 1fr),
    [Code], [Meaning and where it occurs],
    [#cmd("125")], [The data connection is open and the transfer starts: the
      start of #cmd("RETR"), #cmd("STOR") and #cmd("APPE") for data sets, of
      job submission, and of job lists and spool output.],
    [#cmd("150")], [The transfer starts. Used by the UFS view.],
    [#cmd("200")], [The command is accepted. #cmd("SITE") also uses it with a
      second line to name a problem with a word of the command
      (@ref-site-reply).],
    [#cmd("202")], [Nothing to do: bare #cmd("SITE"), and #cmd("OPTS").],
    [#cmd("211")], [The reply to #cmd("STAT") and #cmd("FEAT").],
    [#cmd("213")], [The size or the time of a UFS file.],
    [#cmd("214")], [The reply to #cmd("HELP").],
    [#cmd("215")], [The reply to #cmd("SYST").],
    [#cmd("220")], [The connection is ready. The text is the parameter
      #cmd("BANNER").],
    [#cmd("221")], [The reply to #cmd("QUIT").],
    [#cmd("226")], [The transfer is complete, or #cmd("ABOR") has closed the
      data connection.],
    [#cmd("227"), #cmd("229")], [The address and the port for passive mode
      (#cmd("PASV"), #cmd("EPSV")).],
    [#cmd("230")], [The user is logged in.],
    [#cmd("250")], [The command is done: #cmd("CWD"), #cmd("DELE"),
      #cmd("RMD"), #cmd("STRU"), a transfer of a data set, the end of a job
      list.],
    [#cmd("257")], [The reply to #cmd("PWD") and #cmd("MKD").],
    [#cmd("331")], [The user ID is accepted; send the password.],
    [#cmd("350")], [#cmd("RNFR"): the name is accepted.],
    [#cmd("421")], [FTPD closes the connection. Either the session was idle
      for #cmd("IDLETIMEOUT") seconds: #cmd("421 Idle timeout, closing connection"),
      or the worker pool refused the new session: #cmd("421 Service not
      available, closing control connection").],
    [#cmd("425")], [FTPD cannot open the data connection (@ref-reply-425).],
    [#cmd("426")], [The data connection broke during a transfer. The text says
      after how many bytes and, for data sets and UFS files, that the target is
      incomplete.],
    [#cmd("451")], [Local error. FTPD could not do the work, and a retry may
      work. See @ref-reply-451.],
    [#cmd("452")], [UFS only: no space on the device for a command other than
      the write of a file. A retry later may work.],
    [#cmd("500")], [The command is unknown, or not available in the current
      state of the session (@ref-cmd).],
    [#cmd("501")], [A parameter is missing or wrong (@ref-reply-501).],
    [#cmd("502")], [The command does not exist for this view: #cmd("SIZE") and
      #cmd("MDTM") for data sets, #cmd("APPE") and #cmd("RNTO") for UFS.],
    [#cmd("503")], [#cmd("RNTO") without a #cmd("RNFR") just before, in the MVS
      view: #cmd("503 RNFR not received.")],
    [#cmd("504")], [A parameter is not supported: #cmd("AUTH"),
      #cmd("MODE"), #cmd("STRU"), #cmd("TYPE E"), #cmd("PROT").],
    [#cmd("522")], [#cmd("EPRT") is not supported.],
    [#cmd("530")], [Not logged in, or the login failed (@ref-reply-530).],
    [#cmd("550")], [The action did not happen: the name does not exist, access
      is denied, the name exists already, the data set is under retention
      (@ref-site-retained), or the view does not fit.],
    [#cmd("552")], [The storage or the directory is used up and a retry gives the same answer:
      @ref-reply-552.],
    [#cmd("553")], [UFS only: the path name is too long.],
  )
]

FTPD sends no #cmd("450"). A login failure is always #cmd("530").

== The 425 Reply <ref-reply-425>

#cmd("425 Cannot open data connection") says that FTPD could not open the
data connection for a transfer. After #cmd("PASV") or #cmd("EPSV") FTPD waits
30 seconds for the client to connect to the port. After #cmd("PORT") FTPD
connects to the client, and the connection can be refused. A data connection
serves one transfer: the client sends a new #cmd("PORT"), #cmd("PASV") or
#cmd("EPSV") for the next one.

#cmd("425 Cannot open passive connection") is the reply of #cmd("PASV") and
#cmd("EPSV") when FTPD cannot set up the listening port. The causes are that
no port in #cmd("PASVPORTS") is free, that #cmd("PASVBIND") is not an address of
the host, and a failure of the socket calls. The log in #cmd("STDOUT") says
which.

== The 451 Reply <ref-reply-451>

#cmd("451") is the reply for a problem on the server that is not a refusal. The
causes are:

#tab(caption: [Causes of the 451 reply])[
  #table(columns: (2.3in, 1fr),
    [Text (beginning)], [Cause],
    [#cmd("Requested action aborted: local error in processing.")],
      [The command ended with an ABEND that is not about space or about a full directory. FTPD caught it
      (message #cmd("FTPD070E")); the session goes on.],
    [#cmd("Insufficient storage to list NAME.")],
      [The started task ran out of storage while it built a list of data sets.],
    [#cmd("Read error on data set after N bytes: transfer is incomplete.")],
      [#cmd("RETR") of a data set could not read to the end.],
    [#cmd("Write error on data set after N bytes received: the data set is incomplete.")], [#cmd("STOR") or #cmd("APPE") could not write.],
    [#cmd("Write failed on the server after N bytes received: the file is incomplete.")], [#cmd("STOR") in UFS failed and UFSD gave no reason.],
    [#cmd("Unable to open JES checkpoint")],
      [The job list could not read JES2. A cause is that the procedure of the started task
      lacks the checkpoint data sets.],
    [#cmd("Unable to open JES internal reader"), #cmd("Failed to submit JCL to internal reader"), #cmd("Failed to close internal reader")],
      [Submission of a job failed.],
    [#cmd("Error reading spool data set for JOBnnnnn.")],
      [#cmd("RETR") of job output could not read the spool.],
    [the text of UFSD (#cmd("I/O error"), #cmd("Bad file descriptor"), #cmd("UFS error"))], [UFSD reported an error that has no code of its own.],
  )
]

== The 501 Reply <ref-reply-501>

#cmd("501") means that a parameter is missing or wrong. These are the forms:

- #cmd("501 Invalid dataset name"): the name is longer than 44 characters.
- #cmd("501 Invalid data set name \"NAME\".  Use MVS Dsname conventions."): the
  name holds a character that a data set name cannot hold.
- #cmd("501 A qualifier in \"NAME\" contains an invalid character"): a wildcard
  in a command that does not take one.
- #cmd("501 Missing job ID"): in the JES view, #cmd("RETR") or #cmd("DELE")
  without an argument.
- #cmd("501 PORT not allowed after EPSV ALL"), #cmd("501 PASV not allowed after EPSV ALL"), #cmd("501 Syntax error in PORT parameters"): see @ref-cmd.
- #cmd("501-missing type parameter"), #cmd("501-unknown type ..."): see
  @ref-cmd-type.
- #cmd("501 PROT requires a protection level") with #cmd("SSLPROXY=YES").

== The 530 Reply <ref-reply-530>

#idx("530")
#tab(caption: [Forms of the 530 reply])[
  #table(columns: (2.5in, 1fr),
    [Text], [Cause],
    [#cmd("530 Not logged in.")], [A command that needs a login came first.],
    [#cmd("530 Login incorrect.")], [RAKF refused the user ID or the
      password. The client may try again.],
    [#cmd("530 Login incorrect. Too many attempts, disconnecting.")],
      [The third failed attempt. FTPD closes the connection.],
    [#cmd("530 Not authorized for FTP access.")], [The password is right, but
      the user has no READ access to #cmd("FTPAUTH") in the class
      #cmd("FACILITY"). It counts as a failed attempt.],
    [#cmd("530 Not authorized for FTP access. Disconnecting.")],
      [The same, at the third attempt.],
  )
]

== The 552 Reply <ref-reply-552>

#idx("552")
A #cmd("552") says that the target is full and that the same command fails
again unless something changes. FTPD sends it in these cases:

#tab(caption: [Causes of the 552 reply])[
  #table(columns: (2.2in, 1fr),
    [Text (beginning)], [Cause],
    [#cmd("Requested file action aborted: out of space on the data set. Allocate more space with SITE PRIMARY/SECONDARY and CYLINDERS, or ask the administrator to raise DEFPRIMARY.")],
      [A write to a data set ended with no space left. With an abend code
      (#cmd("(ABEND SB37)")) when the system ended the write with B37, D37 or
      E37.],
    [#cmd("Requested file action aborted: the directory of the PDS is most likely full (ABEND SB14). It cannot be extended; create the PDS with more directory blocks (SITE DIRECTORY=n, then MKD) and copy the members.")],
      [A member #cmd("STOR") or #cmd("APPE") ended with the ABEND SB14. FTPD
      takes it for a full directory of the partitioned data set, as the system
      found no room for the entry of the member. "Most likely" because FTPD does
      not get the reason code, which is in message #cmd("IEC217I")
      (#cmd("B14-0C")). The directory cannot be extended: create the data set
      again with #cmd("SITE DIRECTORY=n") and #cmd("MKD"), and copy the
      members. The partitioned data set is unchanged. See "When the Directory
      Is Full" in the _FTPD User's Guide_.],
    [#cmd("Requested file action aborted: no space for the file. ...")],
      [A write to a UFS file found the file system full, or the file at its
      largest size.],
    [#cmd("No inodes available")], [UFS has no free inode for a new file or
      directory. Before FTPD 1.3.0 the code was #cmd("452").],
  )
]
