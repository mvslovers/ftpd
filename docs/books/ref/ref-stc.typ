#import "../bookmaster/bookmaster.typ": *

= The Started Task <ref-stc>

#idx("started task", "reference")
This chapter describes FTPD as a program in an address space: the procedure,
the authorization, the identity, the threads, the order of the start and the
return codes. The _FTPD User's Guide_ has the daily use, in "Operating FTPD".

== The Procedure <ref-stc-proc>

The sample procedure is the member `samplib/ftpd` of the repository. It runs
#cmd("PGM=FTPD") with #cmd("REGION=8M") and #cmd("TIME=1440"), and it has two
symbolic parameters: #cmd("M") (the member of the configuration, default
#cmd("FTPDPRM0")) and #cmd("D") (the data set that holds it, default
#cmd("SYS2.PARMLIB")).

#tab(caption: [DD names that FTPD uses])[
  #table(columns: (1.2in, 1fr),
    [DD name], [Use],
    [#cmd("STEPLIB")], [The load library with the module #cmd("FTPD").],
    [#cmd("FTPDPRM")], [The configuration (@ref-prm).],
    [#cmd("STDOUT")], [The log: lines of the form
      #cmd("[LEVEL] function: text"), with the levels ERROR, WARN and INFO, and
      the output of #cmd("TRACE DUMP").],
    [#cmd("SYSUDUMP")], [The dump of an abnormal end of the address space.],
    [#cmd("HASPCKPT"), #cmd("HASPACE1")], [Optional. The JES2 checkpoint and the
      spool. The JES view finds both itself, and a DD overrides the search for
      its data set. The _FTPD User's Guide_, "The Procedure and JES", says when to add
      them.],
  )
]

#cmd("STDERR") is in the sample, and no code of FTPD writes to it.

== Authorization <ref-stc-apf>

The module is link-edited with the attribute AC(1). The job step runs in
problem state with the key 8. FTPD asks first whether the step was authorized
when the program was fetched, and then lets the library of the C runtime set up
the authorization through the SVC. The message says which way it was:

#tab(caption: [Messages about the authorization])[
  #table(columns: (0.85in, 1fr),
    [Message], [Meaning],
    [#cmd("FTPD008I AUTHORIZED BY LIBRARY (MODULE KEY 0)")], [The library of
      the load module is APF authorized. The system fetched the module into
      storage of the key 0.],
    [#cmd("FTPD008I AUTHORIZED BY SVC (MODULE KEY 8)")], [The library is not
      APF authorized, and the SVC of the C runtime authorized the task.],
    [#cmd("FTPD003W APF SETUP FAILED RC=n")], [The authorization failed. FTPD
      goes on. The commands that need it fail one at a time.],
  )
]

== The Identity of the Started Task <ref-stc-id>

FTPD does not log on as a user at start. Since FTPD 1.3.0 the started task
keeps the identity that RAKF gave it when the system created it. Before FTPD
1.3.0 FTPD logged on at start as the user #cmd("FTPD") in the group
#cmd("USER"). FTPD reads the identity before it creates any other thread and
writes it:

#tab(caption: [Messages about the identity])[
  #table(columns: (0.85in, 1fr),
    [Message], [Meaning],
    [#cmd("FTPD004I STC RUNS AS user/group")], [The user ID and the group of the
      security environment of the address space.],
    [#cmd("FTPD004W NO SECURITY ENVIRONMENT FOR THE STC: RAKF PERMITS ANY ACCESS WITHOUT AN ACEE")], [The address space has no security environment. RAKF
      permits every access that is checked without a security environment.],
  )
]

A session works under the identity of its own user for the time of a command
that needs it. FTPD puts the identity of the started task back after each such
operation and after each ABEND that it recovers from. The meaning for the
security of the host is in the _FTPD User's Guide_, in "Security".

== The Order of the Start <ref-stc-start>

#tab(caption: [The start of FTPD])[
  #table(columns: (0.4in, 0.9in, 1fr),
    [Step], [Message], [What happens],
    [1], [], [FTPD gets the communication area of the address space and sets the
      limit of the console queue. If the area is not available: #cmd("FTPD090E COM AREA NOT AVAILABLE") and the return code 8.],
    [2], [#cmd("FTPD000I"), #cmd("FTPD005I"), #cmd("FTPD006W")], [The banner:
      the version and the commit of FTPD, the version of the C library, and, if
      the program was built from a modified tree, the warning.],
    [3], [#cmd("FTPD008I"), #cmd("FTPD003W")], [The result of the authorization
      (@ref-stc-apf).],
    [4], [#cmd("FTPD004I"), #cmd("FTPD004W")], [The identity (@ref-stc-id).],
    [5], [#cmd("FTPD009E"), #cmd("FTPD059W"), #cmd("FTPD060W"),
      #cmd("FTPD062W")], [FTPD reads the
      configuration (@ref-prm). A read error ends the start (see below). The
      first two warnings are about #cmd("DEFVOLUME") and #cmd("DEFUNIT")
      (@ref-prm-def). #cmd("FTPD062W") counts the lines of the file that name
      an ignored setting (@ref-prm-ignored).],
    [6], [#cmd("FTPD007W")], [Two lines, only with #cmd("SSLPROXY=YES").],
    [7], [#cmd("FTPD002E")], [FTPD takes the lock for the port (see below). If
      another FTPD holds it, the start ends with the return code 8.],
    [8], [], [FTPD starts the socket thread, which opens the listener, and the
      pool of worker threads.],
    [9], [#cmd("FTPD050E") to #cmd("FTPD056E"), #cmd("FTPD053I"),
      #cmd("FTPD054I")], [The listener (see "When the Port Is Busy" in the _FTPD User's Guide_). #cmd("FTPD054I") says
      that FTPD listens.],
    [10], [#cmd("FTPD001I")], [The start is complete. Clients can connect.],
  )
]

A failure that has a return code gives the return code of the step
(@ref-stc-rc). If the configuration cannot be read, the lock is taken or the
pool of worker threads cannot be created, the message #cmd("FTPD091E INITIALIZATION FAILED RC=n") follows the message of the failure.

FTPD waits for the socket thread to open the listener. How long it waits follows
from #cmd("BINDTRIES") and #cmd("BINDWAIT"). If the socket thread ends without
a listener, FTPD writes #cmd("FTPD056E FTPD IS NOT LISTENING ON PORT n, THIS INSTANCE ENDS") and stops with the return code 8, and #cmd("FTPD001I") is not written.

=== The lock for the port <ref-stc-enq>

#idx("ENQ")
FTPD takes an exclusive ENQ on the resource #cmd("FTPD.PORT.nnnnn") (five
digits, the port with leading zeros) of the major name #cmd("FTPD"), with the
scope SYSTEM. It does not wait for the resource. The system releases the ENQ when
the task ends, also after an ABEND. A second FTPD for the same port ends with
#cmd("FTPD002E"). A second FTPD for another port starts.

== The Threads <ref-stc-thr>

FTPD has three kinds of tasks:

- The main task reads the console (@ref-con) and waits for the end. It looks at
  the console once each second.
- The socket thread opens the listener, accepts the connections and hands each
  to a worker. It looks once each second, and at each look it accepts every
  connection that waits. Once the main task has read a stop request, the
  socket thread ends its wait at once and takes no further connection after
  the one in hand. Before FTPD 1.3.0, it accepted one connection at each look.
- The worker threads run the sessions. FTPD creates the pool with
  #cmd("MAXSESSIONS") as its size, but with at least 4 threads. One worker serves
  one session until the client quits or the connection closes.

When #cmd("MAXSESSIONS") is below 4, FTPD writes the warning
#cmd("[WARN ] initialize: MAXSESSIONS=n is below 4, worker pool raised to 4, at most n sessions are accepted")
to #cmd("STDOUT") at the start. It has no number and no console message. The
session limit stays #cmd("MAXSESSIONS").

The socket thread closes a connection at once, without a reply, when
#cmd("MAXSESSIONS") sessions are active already. A connection that it has
handed to the pool and that no worker has started yet counts as active. It
writes a warning to #cmd("STDOUT").

A worker thread runs every FTP command under the recovery of the C library.
The ABEND of a command is described in the _FTPD User's Guide_, in "When a
Command Abends", and its messages are in @apx-messages.

== Return Codes <ref-stc-rc>

#tab(caption: [The return code of the step])[
  #table(columns: (0.6in, 1fr),
    [Code], [Cause],
    [0], [FTPD ended after #cmd("/P FTPD") or #cmd("SHUTDOWN"). Also a stop that
      came before the listener was ready.],
    [4], [FTPD could not read the configuration (#cmd("FTPD009E")).
      #cmd("FTPD091E INITIALIZATION FAILED RC=4") follows.],
    [8], [The communication area is not available (#cmd("FTPD090E")), another
      FTPD holds the port (#cmd("FTPD002E")), FTPD could not create the pool of
      worker threads, or FTPD is not listening (#cmd("FTPD056E")).],
  )
]

If FTPD cannot create the pool of worker threads, it writes an error to
#cmd("STDOUT") and the console shows #cmd("FTPD091E INITIALIZATION FAILED RC=8").
