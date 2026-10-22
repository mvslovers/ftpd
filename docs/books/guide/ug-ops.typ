#import "../bookmaster/bookmaster.typ": *

= Running FTPD <ug-ops>

#idx("operating")#idx("started task")
FTPD runs as a started task. The operator starts it, asks it questions and
stops it from the console. This chapter covers the daily work: starting and
stopping, the messages at start, the configuration file, the console
commands, and what FTPD does when a command ends in an ABEND. The
installation is in @ug-install and the removal in @ug-remove. The message texts and the
parameters are listed in full in the _FTPD Reference_.

== Starting and Stopping <ug-ops-start>

#idx("start")#idx("stop")#idx("procedure")
FTPD runs from a cataloged procedure. The sample is `samplib/ftpd` in the
repository. It runs the program #cmd("FTPD") with #cmd("REGION=8M") and has
these DD statements:

#tab(caption: [The DD statements of the FTPD procedure])[
  #table(columns: (1.2in, 1fr),
    [DD name], [Use],
    [#cmd("STEPLIB")], [The load library with the module #cmd("FTPD").],
    [#cmd("FTPDPRM")], [The configuration (@ug-ops-config). The sample reads
      the member #cmd("&M") of the data set #cmd("&D").],
    [#cmd("STDOUT")], [The log of FTPD (@ug-ops-log).],
    [#cmd("STDERR")], [Allocated by the sample. The code of FTPD does not write
      to it.],
    [#cmd("SYSUDUMP")], [A dump, if the address space abends.],
    [#cmd("HASPCKPT"), #cmd("HASPACE1")], [Optional. The JES view (@ug-jes)
      finds the JES2 checkpoint and spool data sets itself. Add the statements
      only for a JES2 with another #cmd("DSNPRFX") than #cmd("SYS1"), or a
      checkpoint that is neither cataloged nor on an online volume
      (@ug-install-jes).],
  )
]

The procedure has two symbolic parameters: #cmd("M") is the member with the
configuration and #cmd("D") is the data set that holds it.

```
/S FTPD
/S FTPD,M=FTPDPRM1
/S FTPD,D='MY.PARMLIB'
```

You stop FTPD with #cmd("/P FTPD") or with #cmd("/F FTPD,SHUTDOWN"). Both do
the same. FTPD answers #cmd("FTPD098I FTPD SHUTTING DOWN"), stops the
listener and the worker threads, and ends with #cmd("FTPD099I FTPD SHUTDOWN COMPLETE").

#note[*One FTPD for each port.* FTPD takes an ENQ on the port when it starts.
A second FTPD for the same port ends at once with #cmd("FTPD002E FTPD IS ALREADY ACTIVE ON PORT 2121, THIS INSTANCE ENDS") and the return code 8. A
second FTPD for another port, with its own configuration member, starts
normally.]

The return code of the step is the only part of a start that an automation
product can read without the console:

#tab(caption: [The return code of the FTPD step])[
  #table(columns: (0.8in, 1fr),
    [Code], [Meaning],
    [0], [FTPD ran and was stopped with #cmd("/P") or #cmd("SHUTDOWN"). A stop
      during the bind retry (@ug-ops-bind) is also 0.],
    [4], [FTPD could not read the configuration. #cmd("FTPD009E") and
      #cmd("FTPD091E INITIALIZATION FAILED RC=4") tell it.],
    [8], [FTPD did not start: another FTPD holds the port (#cmd("FTPD002E")), the
      address space has no communication area (#cmd("FTPD090E")), FTPD
      could not listen (#cmd("FTPD056E")), or FTPD could not create its worker
      pool (the log at #cmd("STDOUT") says "failed to create thread
      manager").],
  )
]

== Checking the Messages at Start <ug-ops-msgs>

#idx("messages", "startup")
Every message of FTPD starts with #cmd("FTPD") and a number, and ends with a
letter for the severity: #cmd("I") information, #cmd("W") warning, #cmd("E")
error. The console shows them in upper case. A normal start writes these
messages in this order:

#tab(caption: [The messages of a normal start])[
  #table(columns: (0.8in, 1fr),
    [Message], [Meaning],
    [#cmd("FTPD000I")], [FTPD is starting. The message has the version and the
      commit that the program was built from.],
    [#cmd("FTPD005I")], [The version of the C library (libc370) that FTPD
      contains.],
    [#cmd("FTPD006W")], [Only when the program was built from a modified working
      tree. The commit then does not describe the code.],
    [#cmd("FTPD008I")], [FTPD is authorized, by the library (module key 0) or by
      the SVC (module key 8). #cmd("FTPD003W") means that the setup of the
      authorization failed.],
    [#cmd("FTPD004I")], [The identity of the started task
      (@ug-security-stc). #cmd("FTPD004W") means that the address space has
      no security environment.],
    [#cmd("FTPD007W")], [Twice, only with #cmd("SSLPROXY=YES"). FTPD
      acknowledges #cmd("PBSZ") and #cmd("PROT") and encrypts nothing. A TLS
      proxy has to stand in front of the port.],
    [#cmd("FTPD054I")], [FTPD listens on the address and port.],
    [#cmd("FTPD001I")], [FTPD is ready. Clients can connect.],
  )
]

#cmd("FTPD001I") is written only when the listener works. If FTPD cannot
listen, the messages in @ug-ops-bind explain why, and the instance ends with
#cmd("FTPD056E") and the return code 8. There is no console command that
tries again. Stop and start the started task.

The messages #cmd("FTPD059W") and #cmd("FTPD060W") come with the configuration:
the volume in #cmd("DEFVOLUME") is not online, or the unit in #cmd("DEFUNIT")
does not fit the volume. FTPD then ignores the value and lets the system
choose, so that an upload does not fail for a wrong default.

The message #cmd("FTPD062W n SETTING(S) IN DD:FTPDPRM HAVE NO EFFECT AND WERE IGNORED -- SEE THE LOG")
comes with the configuration too. It counts the lines of the file that name
a setting FTPD no longer has (@ug-ops-config). The log names them. FTPD is not
affected. Remove the lines to stop the message.

== Configuring FTPD <ug-ops-config>

#idx("configuration")#idx("FTPDPRM")
FTPD reads its configuration once, when it starts, from the DD name
#cmd("FTPDPRM"). A change takes effect with the next start. There is no
command to read the file again.

The file has one setting on each line, in the form #cmd("KEY=value"). The rules:

- Empty lines are ignored. A line that starts with #cmd("\#") is a comment.
- The key may be in any case. The value is taken as written, with the blanks
  at both ends removed. Only #cmd("SSLPROXY") is strict: its value must be
  #cmd("YES") or #cmd("NO"), in all upper or all lower case. Any other value
  is logged as a warning, and #cmd("SSLPROXY") stays off.
- A line of the form #cmd("VOLSER,UNIT") used to name a DASD volume. No
  function of FTPD reads a volume table from the configuration. FTPD still
  recognises the line, so that an old file loads, and ignores it (see below). A volume for new data sets is chosen with
  #cmd("DEFVOLUME") and #cmd("DEFUNIT").
- The settings #cmd("AUTHUSER") and #cmd("JESINTERFACELEVEL") are ignored in
  the same way. No function of FTPD reads them: a user ID in
  #cmd("AUTHUSER") does not restrict who may stop the server, and
  #cmd("JESINTERFACELEVEL") does not limit the JES view.
- FTPD writes the warning #cmd("AUTHUSER has no effect in this release and is ignored")
  to the log for each of these lines, with the setting (or #cmd("a VOLSER,UNIT volume line")) in its place. At the end of the read it
  writes one console message, #cmd("FTPD062W"), with the number of the lines.
  Before FTPD 1.3.0 FTPD took these lines without a word.
- A key that FTPD does not know gives a warning in the log. A line without an
  equal sign gives a warning too. FTPD goes on in both cases.
- A value that is out of range or unusable is corrected or ignored, usually with
  a warning in the log. The _FTPD Reference_ says what happens for each
  parameter.
- If the DD name #cmd("FTPDPRM") cannot be opened, FTPD logs a warning and
  starts with the defaults. If the file can be opened but not read to its end,
  FTPD writes #cmd("FTPD009E") and ends with the return code 4. A server that
  looks fine and misses part of its configuration would be worse than one that
  does not start.

This is a small file that is enough for most sites:

```
# FTPD configuration
SRVPORT=2121
MAXSESSIONS=10
IDLETIMEOUT=300
PASVPORTS=22000-22200
DEFRECFM=FB
DEFLRECL=80
DEFBLKSIZE=3120
DEFPRIMARY=10
DEFSECONDARY=5
DEFSPACETYPE=TRACK
```

The parameters fall into four groups. The _FTPD Reference_ has every parameter
with its default and its range.

#tab(caption: [The groups of configuration parameters])[
  #table(columns: (1.4in, 1fr),
    [Group], [Parameters and use],
    [Listener], [#cmd("SRVPORT"), #cmd("SRVBIND") (also #cmd("SRVIP")),
      #cmd("BINDTRIES"), #cmd("BINDWAIT"). The address and port of the control
      connection and the retry (@ug-ops-bind).],
    [Data connections], [#cmd("PASVADR"), #cmd("PASVBIND"), #cmd("PASVPORTS").
      The address and the port range that FTPD offers for passive transfers.
      The range must be open in every firewall between the client and the
      host.],
    [Sessions], [#cmd("MAXSESSIONS"), #cmd("IDLETIMEOUT"), #cmd("MAXRETPD"),
      #cmd("BANNER"), #cmd("SSLPROXY"). #cmd("MAXRETPD") is the most days of
      retention that a client may ask for with #cmd("SITE RETPD") (@ug-site).],
    [New data sets], [#cmd("DEFRECFM"), #cmd("DEFLRECL"), #cmd("DEFBLKSIZE"),
      #cmd("DEFPRIMARY"), #cmd("DEFSECONDARY"), #cmd("DEFSPACETYPE"),
      #cmd("DEFDIRECTORY"), #cmd("DEFUNIT"), #cmd("DEFVOLUME"). The attributes and the space that a
      new data set gets when the client says nothing. The client can change them
      for its session with #cmd("SITE") (@ug-site).],
  )
]

#note[*Passive transfers need the port range.* The default range is 22000 to
22200. Each passive transfer uses one port of the range for its data
connection. If the range is closed on the way from the client to the host, the
login works, and the first #cmd("LIST") or #cmd("RETR") fails.]

#note[*The port.* The default is 2121, not 21. A value that is not a number
makes FTPD log a warning and use the port 21.]

== When the Port Is Busy <ug-ops-bind>

#idx("bind")#idx("port", "busy")
The listener has to get its port from the TCP/IP stack. Right after an IPL, or
right after a stop of FTPD, that can fail for a short time. FTPD does not give up
at once. These messages tell what happens:

#tab(caption: [The messages of the listener])[
  #table(columns: (0.8in, 1fr),
    [Message], [Meaning],
    [#cmd("FTPD051E")], [The call #cmd("bind()") failed. The message has the
      address, the port and the error number.],
    [#cmd("FTPD051I")], [FTPD waits #cmd("BINDWAIT") seconds and tries again.
      The message says which try it is, out of #cmd("BINDTRIES").],
    [#cmd("FTPD053I")], [FTPD closed a socket that an earlier FTPD left on its
      port.],
    [#cmd("FTPD050E"), #cmd("FTPD052E")], [#cmd("socket()") or #cmd("listen()")
      failed. FTPD does not retry.],
    [#cmd("FTPD055E")], [The value of #cmd("SRVBIND") is not an address.],
    [#cmd("FTPD056E")], [FTPD does not listen. The instance ends with the return
      code 8.],
  )
]

FTPD tries again only when the port is in use (#cmd("EADDRINUSE")) or when the
address is not on this host (#cmd("EADDRNOTAVAIL")). The second case happens
when the started task starts before TCP/IP has its address. Every other error
ends the start at once. The defaults are 3 tries and 5 seconds. The time that
FTPD waits is at most #cmd("BINDTRIES") times #cmd("BINDWAIT") seconds. Both
values are limited to 1 to 100.

If the first #cmd("bind()") fails because the port is in use, FTPD looks at its
own sockets before it waits. A socket that an earlier FTPD left on the port,
and that no one can use any more, is closed (#cmd("FTPD053I")), and FTPD binds
again after two seconds. A start that works this way logs no bind error. The
sweep closes the sockets on the port whose address conflicts with
#cmd("SRVBIND"), so do not start FTPD on a port that
another program on the same system uses.

You can stop FTPD with #cmd("/P FTPD") while it waits. It ends after at most one
second of the wait with #cmd("FTPD051I BIND RETRY ABANDONED, FTPD IS STOPPING").

== Controlling FTPD from the Console <ug-ops-con>

#idx("console commands")#idx("MODIFY")
The operator talks to FTPD with the MODIFY command: #cmd("/F FTPD,command").
FTPD changes the command to upper case and removes blanks at both ends. The
answer comes as messages on the console.

#tab(caption: [The console commands])[
  #table(columns: (1.2in, 1fr),
    [Command], [Answer],
    [#cmd("STATS")], [#cmd("FTPD010I") to #cmd("FTPD014I"): the status, the
      number of active and of all sessions since the start, the bytes received
      and sent, and the state of the trace. The bytes are those of the
      sessions that have ended.],
    [#cmd("SESSIONS")], [#cmd("FTPD015I ACTIVE SESSIONS: 2 / 10"): the active
      sessions and #cmd("MAXSESSIONS").],
    [#cmd("CONFIG")], [The configuration that FTPD uses: the messages
      #cmd("FTPD040I") to #cmd("FTPD046I"), #cmd("FTPD049I"),
      #cmd("FTPD057I"), #cmd("FTPD058I") and #cmd("FTPD061I").],
    [#cmd("VERSION")], [Two lines #cmd("FTPD016I"): the version and commit of
      FTPD, and the version of the C library. #cmd("FTPD006W") follows if the
      program was built from a modified tree.],
    [#cmd("TRACE ON"), #cmd("TRACE OFF")], [#cmd("FTPD080I TRACE ENABLED") and
      #cmd("FTPD081I TRACE DISABLED").],
    [#cmd("TRACE DUMP")], [FTPD writes the entries of the trace to the DD name
      #cmd("STDOUT") and answers #cmd("FTPD082I") with their number.],
    [#cmd("HELP")], [#cmd("FTPD020I"): the list of commands.],
    [#cmd("SHUTDOWN")], [The same as #cmd("/P FTPD").],
  )
]

An unknown command gets #cmd("FTPD021E UNKNOWN COMMAND: X"). A #cmd("TRACE")
without #cmd("ON"), #cmd("OFF") or #cmd("DUMP") gets #cmd("FTPD021E TRACE: SYNTAX: TRACE ON|OFF|DUMP"). A command that is longer than 127 characters is
ignored without an answer.

#cmd("CONFIG") shows what FTPD uses after the start, and that is not always
what the file says: a value that FTPD replaced, for example an unusable
#cmd("DEFVOLUME"), shows with the replacement. When a client behaves in a way
that you do not expect, start with #cmd("CONFIG") and #cmd("VERSION").

== Limiting Sessions and Time-outs <ug-ops-ses>

#idx("sessions")#idx("time-out")
Each client session has its own thread. FTPD accepts as many sessions as
#cmd("MAXSESSIONS") says. When the limit is reached, FTPD closes the next
connection at once, without a reply, and writes a warning to the log. The
client sees a closed connection. Sessions that are in the login state count
as well.

#idx("listener")#idx("connection burst")
The listener looks for waiting connections once each second, and a single
connection can wait up to one second for the look. At each look FTPD takes
every connection that waits, not only the first. Clients that connect together
are therefore greeted with #cmd("220") within about one second, and not one
per second. The limit holds for such a burst: FTPD counts a connection that it
has handed to the worker pool, and that no worker has started yet, as a
session. When more clients connect together than #cmd("MAXSESSIONS") allows,
the first ones are greeted and FTPD closes the rest without a reply. This is
the behaviour of FTPD 1.3.0. FTPD 1.2.0 took one connection per second.

FTPD hands each new session to a pool of worker threads. The pool has
#cmd("MAXSESSIONS") workers, so every value from 1 up works. When the pool
cannot take it, because it is shutting down or has no storage for the queue
entry, FTPD answers #cmd("421 Service not available, closing control connection"),
closes the connection, frees the session and writes a warning to the log. The
next connection is served as usual.

A session that sends nothing for #cmd("IDLETIMEOUT") seconds gets
#cmd("421 Idle timeout, closing connection") and FTPD closes it. The default is
300 seconds. A transfer is not idle time, but a client that waits between two
commands is. Raise the value for clients that keep the control connection open
for a long time.

== Reading the Log <ug-ops-log>

#idx("log")#idx("STDOUT")
FTPD writes to two places. The messages with a number go to the console. The
rest, for example the progress of the configuration and the start and end of a
session, goes to the DD name #cmd("STDOUT") as lines of the form

```
[INFO ] function: text
```

The level is ERROR, WARN, INFO or DEBUG. FTPD writes the levels up to INFO.
A line at the end of a session has the user ID and the number of bytes that the
session sent and received. The warnings about the configuration (@ug-ops-config)
and the refusal of a connection at the session limit (@ug-ops-ses) are only in
this log. Look at it when the console is quiet and a client has a problem.

*The JES spool.* FTPD 1.3.0 checks every job that a user touches in the JES
view against the profiles of the class #cmd("JESSPOOL")
(@ug-security-jesspool), and it writes what it refused to this log. The lines
are:

#screen(```
[WARN ] SITE JESOWNER refused: user=BOB value=ALICE rc=4
[WARN ] JES LISTJOB denied: user=BOB job=JOB00038 reason=NOPROFILE rc=4
[WARN ] JES DELE denied: user=AUDIT1 job=JOB00038 reason=NOCANCEL rc=8
[INFO ] JES LIST: 4 jobs, 0 own, 21 hidden, 0 ERR, 11 RACF calls
```)

- *#cmd("SITE JESOWNER refused"):* a #cmd("SITE JESOWNER") that the client got a
  #cmd("501") for. #cmd("value") is what the client sent in upper case, with
  each character other than a letter, a digit, #cmd("$"), #cmd("#") or
  #cmd("@") shown as #cmd("?") and cut after eight characters, so #cmd("*")
  reads #cmd("?").
  #cmd("rc") is the return code of RAKF: 4 when no profile covers the user ID
  and 8 when a profile gives no READ. It is #cmd("-") when FTPD did not ask
  RAKF, as for #cmd("ABC*"), and the log has #cmd("value=ABC?").
- *#cmd("JES op denied"):* a job that FTPD hid from the user or refused to
  cancel. #cmd("op") is #cmd("LISTJOB") for #cmd("dir JOBnnnnn"), #cmd("RETR")
  or #cmd("DELE"). #cmd("reason=NOPROFILE") means that no profile covers the
  job, #cmd("reason=NONE") that a profile gives no access, and
  #cmd("reason=NOCANCEL") that the user has READ and tried to cancel. The
  #cmd("rc") is the RAKF return code again. #cmd("reason=NOACEE") means that
  the session has no security environment, with #cmd("rc=-"), and FTPD
  writes it once for each command.
- *#cmd("JES LIST"):* one line for each #cmd("dir") or #cmd("ls"), at the level
  INFO: the jobs listed, how many of them are the own jobs, how many FTPD hid,
  how many it could not decide (#cmd("ERR")), and the number of questions that
  it put to RAKF. A plain list hides a job without a line of its own. The
  count is in this line.
- *#cmd("JES op error ... reason=ERR"):* RAKF gave an answer that FTPD cannot
  use, or the job has a name that cannot be an entity. FTPD hides the job.
  This line is at the level ERROR, with at most eight lines for one command and
  then #cmd("JES LIST error: user=USERID and n more ERR jobs").

FTPD writes at most 16 lines of the form #cmd("denied") for a session, so that
a client cannot fill the log. When it left more out, it writes
#cmd("JES denied: user=USERID and n more denied") when the session ends. The
line #cmd("SITE JESOWNER refused") counts in the same 16. The line with
#cmd("reason=NOACEE") does not: FTPD writes it once for each command.

== When a Command Abends <ug-ops-abend>

#idx("ABEND")#idx("recovery")
Every FTP command runs under the recovery of the C library. An ABEND
in one command does not end the started task and does not end the other
sessions. The recovery restores the identity of the started task, releases the
lock that protects the identity, tells the client, and closes the data
connection and the file that the command used.

The message on the console has this form:

```
FTPD070E ABEND S0C4 RECOVERED CMD=RETR SOCKET=3 TOTAL=1
```

with the ABEND code, the FTP command, the socket of the session and the number
of recoveries since the start. A user ABEND shows as #cmd("U") and four digits.
If FTPD could not even set up the recovery, the message says so in parentheses,
#cmd("FTPD070E ABEND RECOVERY (ESTAE CREATE FAILED) CMD=... SOCKET=... TOTAL=..."),
and the command did not run.

The first step of the recovery puts the identity of the started task back. If
that step ends in an ABEND itself, FTPD goes on with the recovery and writes
#cmd("FTPD077E IDENTITY RESET ABENDED Sxxx -- ASXBSENV MAY STILL HOLD A USER ACEE")
before the #cmd("FTPD070E"). For a user ABEND the code reads
#cmd("Unnnn"). If the recovery for the reset could not be set up, the reset did
not run, and the text reads #cmd("IDENTITY RESET NOT RUN, ESTAE FAILED") without
a code. The address space may then still carry the security environment of the
user of that session. Stop FTPD, start it again and report the ABEND code, if
there is one. FTPD 1.3.0 writes this message.

The client gets one of three replies:

- *The data set is full:* the ABEND is #cmd("SB37"), #cmd("SD37") or
  #cmd("SE37"). The reply is #cmd("552 Requested file action aborted: out of space on the data set (ABEND SB37). Allocate more space with SITE PRIMARY/SECONDARY and CYLINDERS, or ask the administrator to raise DEFPRIMARY."). The reply is a permanent one, because a client that tries again
  would run into the same limit (@ug-data-space).
- *The directory of a partitioned data set is full:* the ABEND is #cmd("SB14")
  during a #cmd("STOR") or #cmd("APPE"). The reply is #cmd("552 Requested file action aborted: the directory of the PDS is most likely full (ABEND SB14). It cannot be extended; create the PDS with more directory blocks (SITE DIRECTORY=n, then MKD) and copy the members."). It is permanent as well (@ug-data-dirfull).
- *Any other ABEND:* #cmd("451 Requested action aborted: local error in processing.").

When a #cmd("STOR") had just created the data set, FTPD scratches it, because
what is left is incomplete. #cmd("FTPD074I SCRATCHED dsn AFTER A FAILED TRANSFER") tells that it worked. #cmd("FTPD073W COULD NOT SCRATCH dsn, IDCAMS RC=n") means that the data set stays, and the user or the operator has to
delete it. #cmd("FTPD075E") means that the scratch itself ended in an ABEND.
The data set stays in that case too.
#cmd("FTPD076W") tells that the cleanup of the file after the ABEND did not go
cleanly. When the close of the file ended in an ABEND, FTPD looks in the task
I/O table of the started task for the DD of the file and says what it found:

- #cmd("CLOSE ABENDED, DD STILL HELD"): the DD is still in the table. The data
  set stays allocated to the started task, and a scratch of it is refused until
  FTPD ends.
- #cmd("CLOSE ABENDED, DD RELEASED"): the DD is gone. The close ended in an
  ABEND, but the data set is free. The operator has nothing to do. FTPD 1.3.0
  writes this text. Before, it said #cmd("DD STILL HELD") in this case too.
- #cmd("ABANDON AFTER ... RETURNED n -- SEE __fabandon() IN CLIBIO.H"): the
  call that abandons the file returned a negative code.
- #cmd("CLOSE ABENDED, DD STATE UNKNOWN"): FTPD had no DD name to look for, or
  no table to look in, and cannot tell. If a delete of the data set is refused,
  the DD is held.

A #cmd("STOR") into a partitioned data set with a full directory (ABEND SB14)
can write #cmd("FTPD076W") after #cmd("FTPD070E"), when the cleanup of the
file ends in an ABEND again (@ug-data-dirfull).

If a session ends in an ABEND three times in a row, with no good command in
between, FTPD gives up on that session. It writes #cmd("FTPD071E SESSION SOCKET=3 CLOSED AFTER 3 CONSECUTIVE ABENDS") and closes the connection. Other
sessions go on.

#note[Do not ignore #cmd("FTPD070E"). One message is a command that failed.
A message for every session is a defect. Look for the dump in #cmd("SYSUDUMP")
and the line of the failing command in the log, and report the ABEND code with
the FTP command.]
