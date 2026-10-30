#import "../bookmaster/bookmaster.typ": *

= Messages <apx-messages>

#idx("messages", "list")
This appendix lists every console message of FTPD in the order of the numbers.
A message has the form #cmd("FTPDnnnx"). The letter at the end is the severity:
#cmd("I") information, #cmd("W") warning, #cmd("E") error. FTPD writes all text
of a message in upper case, except the values that it shows as they are. FTPD builds
a message in a buffer of 128 characters, so a text of more than 127 characters
is cut.

In the texts, #cmd("n") stands for a number, #cmd("dsn") for a data set name
and #cmd("text") for text that varies. The messages of the log in the DD name
#cmd("STDOUT") have no number and are not listed. The messages that a client
sees are the reply codes in @ref-reply.

== Start and Stop (000 to 009) <apx-messages-start>

#tab(caption: [Messages 000 to 009])[
  #table(columns: (2.5in, 1fr),
    [Message], [Meaning and action],
    [#cmd("FTPD000I FTPD version (commit) STARTING")], [The first message of the
      start. It names the version and the commit of the build.],
    [#cmd("FTPD001I FTPD version READY")], [FTPD listens. Clients can connect.],
    [#cmd("FTPD002E FTPD IS ALREADY ACTIVE ON PORT n, THIS INSTANCE ENDS")],
      [Another FTPD holds the lock for the port. This instance ends with the
      return code 8. Stop the other FTPD, or use another port in the
      configuration (@ref-stc-enq).],
    [#cmd("FTPD003W APF SETUP FAILED RC=n")], [FTPD could not authorize itself.
      FTPD goes on, but nobody can log in: every #cmd("PASS") gets
      #cmd("530") (@ref-reply-530). Fix the cause and start FTPD again
      (@ref-stc-apf).],
    [#cmd("FTPD004I STC RUNS AS user/group")], [The identity of the started task
      (@ref-stc-id).],
    [#cmd("FTPD004W NO SECURITY ENVIRONMENT FOR THE STC: RAKF PERMITS ANY ACCESS WITHOUT AN ACEE")], [The address space has no security environment. Do not
      run FTPD like this in production. Look at how the system starts the
      task.],
    [#cmd("FTPD004E THE SECURITY SYSTEM ACCEPTS ANY USERID AND PASSWORD")], [The
      first of two lines. The check of the security system at the start
      (@ref-stc-check) found that it does not verify the user ID and the
      password. Since FTPD 1.3.3.],
    [#cmd("FTPD004E ALL LOGINS ARE REFUSED: CORRECT THE SECURITY TABLES, THEN RESTART FTPD")], [The second line. FTPD goes on, but nobody can log in:
      every #cmd("PASS") gets #cmd("530") (@ref-reply-530). Correct the
      security tables ("Checking the Security System at Start" in the
      _FTPD User's Guide_), then stop and start FTPD. The
      check is made only at the start.],
    [#cmd("FTPD005I text")], [The version of the C library that FTPD contains.],
    [#cmd("FTPD006W BUILT FROM A MODIFIED WORKING TREE")], [The program was built
      from sources with changes that were not committed. The commit in
      #cmd("FTPD000I") does not describe the code. Not for production. FTPD
      writes the message at the start and again for the console command
      #cmd("VERSION") (@ref-con).],
    [#cmd("FTPD007W SSLPROXY=YES: PBSZ/PROT ARE ACKNOWLEDGED BUT FTPD ENCRYPTS NOTHING")], [The first of two messages at every start with
      #cmd("SSLPROXY=YES").],
    [#cmd("FTPD007W A TLS TERMINATING PROXY MUST BE IN FRONT OF PORT n")], [FTPD
      does not encrypt. Without a TLS proxy in front of the port the data of
      the clients is not protected.],
    [#cmd("FTPD008I AUTHORIZED BY LIBRARY (MODULE KEY 0)")], [The library of
      the load module is APF authorized.],
    [#cmd("FTPD008I AUTHORIZED BY SVC (MODULE KEY 8)")], [The library is not APF
      authorized. The SVC of the C runtime authorized the task.],
    [#cmd("FTPD009E PARMLIB (DD:FTPDPRM) READ FAILED -- CONFIGURATION INCOMPLETE, SHUTTING DOWN")], [FTPD opened the configuration and could not read it to
      the end. The start ends with the return code 4. Check the data set of the
      DD name #cmd("FTPDPRM") and start again.],
  )
]

== Console Commands (010 to 049) <apx-messages-con>

#tab(caption: [Messages 010 to 049])[
  #table(columns: (2.5in, 1fr),
    [Message], [Meaning and action],
    [#cmd("FTPD010I STATUS: ACTIVE") or #cmd("INACTIVE")], [The answer to
      #cmd("STATS"), line 1.],
    [#cmd("FTPD011I SESSIONS:    n ACTIVE, n TOTAL")], [#cmd("STATS"), line 2.],
    [#cmd("FTPD012I BYTES IN:    n")], [#cmd("STATS"), line 3. The bytes
      received on data connections by the sessions that have ended.],
    [#cmd("FTPD013I BYTES OUT:   n")], [#cmd("STATS"), line 4. The bytes
      sent on data connections by the sessions that have ended.],
    [#cmd("FTPD014I TRACE:       ON") or #cmd("OFF")], [#cmd("STATS"), line 5.],
    [#cmd("FTPD015I ACTIVE SESSIONS: n / n")], [The answer to #cmd("SESSIONS").],
    [#cmd("FTPD016I FTPD version (commit)")], [The answer to #cmd("VERSION"),
      line 1.],
    [#cmd("FTPD016I text")], [#cmd("VERSION"), line 2: the version of the C
      library.],
    [#cmd("FTPD020I COMMANDS: STATS, SESSIONS, CONFIG, VERSION, TRACE, HELP, SHUTDOWN")], [The answer to #cmd("HELP").],
    [#cmd("FTPD021E UNKNOWN COMMAND: text")], [The text is not a console command.
      The text is shown in upper case.],
    [#cmd("FTPD021E TRACE: SYNTAX: TRACE ON|OFF|DUMP")], [#cmd("TRACE") with a
      word that is not #cmd("ON"), #cmd("OFF") or #cmd("DUMP"), or with none.],
    [#cmd("FTPD040I") to #cmd("FTPD046I"), #cmd("FTPD049I")], [The answer to
      #cmd("CONFIG"). The messages #cmd("FTPD057I"), #cmd("FTPD058I") and
      #cmd("FTPD061I") belong to it. See @ref-con-config. The messages
      #cmd("FTPD047I") and #cmd("FTPD048I") (the volume lines) are gone since
      FTPD 1.3.0.],
  )
]

== Listener (050 to 062) <apx-messages-net>

#tab(caption: [Messages 050 to 062])[
  #table(columns: (2.5in, 1fr),
    [Message], [Meaning and action],
    [#cmd("FTPD050E SOCKET() FAILED, ERRNO=n")], [FTPD could not create the
      listener socket. No retry. FTPD ends with the return code 8 after
      #cmd("FTPD056E"). Look at the state of TCP/IP.],
    [#cmd("FTPD051E BIND() FAILED ON address PORT n, ERRNO=n")], [The first
      #cmd("bind()") failed. FTPD retries only for the errors "port in use" and
      "address not on this host" (see "When the Port Is Busy" in the _FTPD User's
      Guide_).],
    [#cmd("FTPD051I RETRYING BIND IN ns (n OF n)")], [FTPD waits and tries
      again. The numbers are #cmd("BINDWAIT"), the number of this try and
      #cmd("BINDTRIES").],
    [#cmd("FTPD051I BIND RETRY ABANDONED, FTPD IS STOPPING")], [The operator
      stopped FTPD while it waited.],
    [#cmd("FTPD051E BIND() STILL FAILING AFTER n TRIES, ERRNO=n")], [All tries
      failed. FTPD ends with the return code 8 after #cmd("FTPD056E"). Free the
      port, or raise #cmd("BINDTRIES") if TCP/IP is not up yet at the start.],
    [#cmd("FTPD052E LISTEN() FAILED, ERRNO=n")], [FTPD could not listen. No
      retry.],
    [#cmd("FTPD053I CLOSING STALE SOCKET n ON address PORT n")], [FTPD found a
      socket that an earlier FTPD left on its port and closed it.],
    [#cmd("FTPD053I n STALE SOCKETS CLOSED ON PORT n")], [The summary, only when
      FTPD closed more than one.],
    [#cmd("FTPD054I LISTENING ON address PORT n")], [The listener works.],
    [#cmd("FTPD055E SRVBIND text IS NOT AN ADDRESS, NOT LISTENING")], [The
      address cannot be used. The configuration check
      (@ref-prm-net) normally replaces such a value with ANY, so this message
      is a last line of defense.],
    [#cmd("FTPD056E FTPD IS NOT LISTENING ON PORT n, THIS INSTANCE ENDS")],
      [The listener did not come up. The return code is 8.],
    [#cmd("FTPD057I   BINDTRIES=n BINDWAIT=n")], [Part of #cmd("CONFIG").],
    [#cmd("FTPD058I   DEFPRIMARY=n DEFSECONDARY=n DEFSPACETYPE=text DEFDIRECTORY=n")], [Part of
      #cmd("CONFIG"). #cmd("DEFDIRECTORY") is in the line since FTPD 1.3.0.],
    [#cmd("FTPD059W DEFVOLUME=volume IS NOT ONLINE -- NEW DATA SETS WILL GO WHERE THE SYSTEM CHOOSES")], [The volume is not online. FTPD drops
      #cmd("DEFVOLUME") and #cmd("DEFUNIT"). Correct the parameter or mount the
      volume (@ref-prm-def).],
    [#cmd("FTPD060W DEFUNIT=unit DOES NOT MATCH DEFVOLUME=volume (A type) -- UNIT IGNORED")], [The unit has four digits and they differ from the device type
      of the volume. FTPD drops #cmd("DEFUNIT").],
    [#cmd("FTPD061I   MAXRETPD=n")], [Part of #cmd("CONFIG"). The most days that
      #cmd("SITE RETPD") takes (@ref-site-retpd-limit). The message is new in
      FTPD 1.3.0.],
    [#cmd("FTPD062W n SETTING(S) IN DD:FTPDPRM HAVE NO EFFECT AND WERE IGNORED -- SEE THE LOG")],
      [FTPD read n lines of #cmd("FTPDPRM") that name a setting it does not
      have: #cmd("AUTHUSER"), #cmd("JESINTERFACELEVEL") or a volume line
      (@ref-prm-ignored). Each is named in the log (DD name #cmd("STDOUT")). FTPD
      starts as it would without the lines. Remove them from the file. The
      message is new in FTPD 1.3.0. It is not written when there is no such
      line.],
  )
]

== Recovery (070 to 077) <apx-messages-rec>

#tab(caption: [Messages 070 to 077])[
  #table(columns: (2.5in, 1fr),
    [Message], [Meaning and action],
    [#cmd("FTPD070E ABEND Sxxx RECOVERED CMD=verb SOCKET=n TOTAL=n")], [A command
      ended in a system ABEND, and FTPD recovered. The ABEND code, the FTP
      command, the socket of the session and the number of recoveries since
      the start follow. For #cmd("SB37"), #cmd("SD37") and #cmd("SE37") the
      client gets #cmd("552"), and for #cmd("SB14") during a #cmd("STOR") or
      #cmd("APPE") as well (a full directory). For all others the client gets #cmd("451") (see "When a Command Abends" in the _FTPD User's Guide_).],
    [#cmd("FTPD070E ABEND Unnnn RECOVERED CMD=verb SOCKET=n TOTAL=n")], [The same
      for a user ABEND.],
    [#cmd("FTPD070E ABEND RECOVERY (ESTAE CREATE FAILED) CMD=verb SOCKET=n TOTAL=n")], [FTPD could not set up the recovery for the command.],
    [#cmd("FTPD071E SESSION SOCKET=n CLOSED AFTER n CONSECUTIVE ABENDS")], [The
      session ended in an ABEND three times in a row. FTPD closes it. Other
      sessions go on.],
    [#cmd("FTPD072W DEBUG ABEND ...")], [Only in a test build with the compile
      option #cmd("FTPD_DEBUG_ABEND"). A released FTPD does not write it.],
    [#cmd("FTPD073W COULD NOT SCRATCH dsn, IDCAMS RC=n")], [FTPD wanted to delete
      the data set that a failed #cmd("STOR") had created, and the delete
      failed. The data set stays and is incomplete. Delete it by hand.],
    [#cmd("FTPD074I SCRATCHED dsn AFTER A FAILED TRANSFER")], [FTPD deleted the
      incomplete data set.],
    [#cmd("FTPD075E ABEND WHILE SCRATCHING dsn -- IDENTITY RESTORED, DATA SET LEFT")], [The delete itself ended in an ABEND. The identity of the started
      task is back. The data set stays. Delete it by hand.],
    [#cmd("FTPD076W ABANDON AFTER verb RETURNED n -- CLOSE ABENDED, DD STILL HELD")], [The close of the file ended in an ABEND, and the task I/O table of
      the started task still holds a DD of the name of the file (n is greater
      than 0). The data set stays allocated to the started task, and a delete
      of it is refused until FTPD ends. The name is also found when another
      session has allocated a DD of the same name in the meantime.],
    [#cmd("FTPD076W ABANDON AFTER verb RETURNED n -- CLOSE ABENDED, DD RELEASED")], [The close of the file ended in an ABEND, but the task I/O table no longer
      holds the DD (n is greater than 0). The data set is free. No action is
      needed. Since FTPD 1.3.0. Earlier releases wrote #cmd("DD STILL HELD") in
      this case too.],
    [#cmd("FTPD076W ABANDON AFTER verb RETURNED n -- CLOSE ABENDED, DD STATE UNKNOWN")], [The close of the file ended in an ABEND (n is greater than 0), but FTPD had
      no DD name to look for or no task I/O table to look in. It cannot tell
      whether the data set is held. If a delete of the data set is refused, it
      is. Since FTPD 1.3.0.],
    [#cmd("FTPD076W ABANDON AFTER verb RETURNED n -- SEE __fabandon() IN CLIBIO.H")], [The cleanup of the file returned an error code that the
      header #cmd("clibio.h") of the C library describes (n is less than 0).],
    [#cmd("FTPD077E IDENTITY RESET ABENDED Sxxx -- ASXBSENV MAY STILL HOLD A USER ACEE")], [While FTPD recovered from the ABEND of a command, the reset of the
      identity of the started task ended in a system ABEND itself. Sxxx is
      its code. The field #cmd("ASXBSENV") of the address space may still hold
      the ACEE of the user of that session, and until it is written again the
      other sessions authorize as that user. The recovery goes on: the lock
      that protects the identity is released, #cmd("FTPD070E") is written, and
      the client is answered. FTPD writes #cmd("FTPD077E") before
      #cmd("FTPD070E"), and only on a task that is APF authorized. An FTPD
      that is not authorized (see #cmd("FTPD003W")) does not write it. Stop
      FTPD, start it again and report the ABEND code. Since FTPD 1.3.0.],
    [#cmd("FTPD077E IDENTITY RESET ABENDED Unnnn -- ASXBSENV MAY STILL HOLD A USER ACEE")], [The same for a user ABEND. Unnnn is its code.],
    [#cmd("FTPD077E IDENTITY RESET NOT RUN, ESTAE FAILED -- ASXBSENV MAY STILL HOLD A USER ACEE")], [FTPD could not set up the recovery for the reset, so the reset did
      not run. The field #cmd("ASXBSENV") may hold the ACEE of a user, as
      above, and the recovery goes on in the same way. There is no ABEND code.
      Stop FTPD and start it again.],
  )
]

== Trace (080 to 082) <apx-messages-trc>

#tab(caption: [Messages 080 to 082])[
  #table(columns: (2.5in, 1fr),
    [Message], [Meaning and action],
    [#cmd("FTPD080I TRACE ENABLED")], [The answer to #cmd("TRACE ON").],
    [#cmd("FTPD081I TRACE DISABLED")], [The answer to #cmd("TRACE OFF").],
    [#cmd("FTPD082I TRACE DUMPED, n ENTRIES")], [The answer to
      #cmd("TRACE DUMP"). The entries are in the DD name #cmd("STDOUT").],
  )
]

== Initialization and Shutdown (090 to 099) <apx-messages-end>

#tab(caption: [Messages 090 to 099])[
  #table(columns: (2.5in, 1fr),
    [Message], [Meaning and action],
    [#cmd("FTPD090E COM AREA NOT AVAILABLE")], [The address space has no
      communication area for console commands. The start ends with the return
      code 8.],
    [#cmd("FTPD091E INITIALIZATION FAILED RC=n")], [The start ended because the
      configuration could not be read (4), another FTPD holds the port (8) or
      the worker threads could not be created (8).],
    [#cmd("FTPD095I WAITING FOR SOCKET THREAD TO TERMINATE (n)")], [The shutdown
      waits for the thread that listens. It waits at most ten seconds.],
    [#cmd("FTPD095W SOCKET THREAD DID NOT TERMINATE")], [The wait ran out. FTPD
      goes on with the shutdown.],
    [#cmd("FTPD098I FTPD SHUTTING DOWN")], [The answer to #cmd("/P FTPD") and
      #cmd("SHUTDOWN").],
    [#cmd("FTPD099I FTPD SHUTDOWN COMPLETE")], [The last message of FTPD.],
  )
]
