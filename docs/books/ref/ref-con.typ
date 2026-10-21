#import "../bookmaster/bookmaster.typ": *

= Console Commands <ref-con>

#idx("console commands", "reference")
The operator controls FTPD from the console with the MODIFY command and the
STOP command. The _FTPD User's Guide_ shows how to use them, in "Operating
FTPD". This chapter lists the syntax and every message that a command writes.

== Syntax <ref-con-syntax>

```
/F FTPD,command
/P FTPD
```

- FTPD changes the text of a MODIFY command to upper case, removes the blanks at
  its start and at its end, and compares it with the names below. A command
  that is not in the list gets #cmd("FTPD021E UNKNOWN COMMAND: X"), where X is
  the text in upper case.
- A text of 128 characters or more, and an empty text, are dropped without an
  answer.
- The answers are messages to the console. None of them goes to the client or
  to the DD name #cmd("STDOUT").
- The main task of FTPD looks for new console commands once each second, so an
  answer can take up to one second.
- #cmd("/P FTPD") and the MODIFY command #cmd("SHUTDOWN") do the same.

== The Commands <ref-con-cmds>

#tab(caption: [Console commands and their messages])[
  #table(columns: (1.15in, 1fr),
    [Command], [Messages],
    [#cmd("STATS")], [Five messages: #cmd("FTPD010I STATUS: ACTIVE"),
      #cmd("FTPD011I SESSIONS:    n ACTIVE, m TOTAL"),
      #cmd("FTPD012I BYTES IN:    n"), #cmd("FTPD013I BYTES OUT:   n") and
      #cmd("FTPD014I TRACE:       ON") or #cmd("OFF"). The counters start at
      zero when FTPD starts. #cmd("TOTAL") counts the sessions since the start.],
    [#cmd("SESSIONS")], [#cmd("FTPD015I ACTIVE SESSIONS: n / m"), the active
      sessions and #cmd("MAXSESSIONS").],
    [#cmd("CONFIG")], [The values that FTPD uses (@ref-con-config).],
    [#cmd("VERSION")], [#cmd("FTPD016I FTPD version (commit)") and
      #cmd("FTPD016I") with the version of the C library. #cmd("FTPD006W BUILT FROM A MODIFIED WORKING TREE") follows if the program was built from a
      working tree with changes.],
    [#cmd("TRACE ON")], [#cmd("FTPD080I TRACE ENABLED"). FTPD collects trace
      entries in a ring buffer of 512 entries.],
    [#cmd("TRACE OFF")], [#cmd("FTPD081I TRACE DISABLED").],
    [#cmd("TRACE DUMP")], [FTPD writes the entries to the DD name #cmd("STDOUT")
      and answers #cmd("FTPD082I TRACE DUMPED, n ENTRIES").],
    [#cmd("TRACE")], [With no word, or with a word other than #cmd("ON"),
      #cmd("OFF") and #cmd("DUMP"): #cmd("FTPD021E TRACE: SYNTAX: TRACE ON|OFF|DUMP").],
    [#cmd("HELP")], [#cmd("FTPD020I COMMANDS: STATS, SESSIONS, CONFIG, VERSION, TRACE, HELP, SHUTDOWN").],
    [#cmd("SHUTDOWN")], [#cmd("FTPD098I FTPD SHUTTING DOWN") now, and
      #cmd("FTPD099I FTPD SHUTDOWN COMPLETE") when the shutdown is done.],
  )
]

#cmd("FTPD014I") shows the state of the trace for the whole address space. The
trace is off after the start.

== The CONFIG Messages <ref-con-config>

#cmd("CONFIG") writes these messages, in this order. The values are the
ones that FTPD uses. A value that FTPD replaced while it read the file shows
with the replacement.

#tab(caption: [The messages of the CONFIG command])[
  #table(columns: (0.85in, 1fr),
    [Message], [Content],
    [#cmd("FTPD040I")], [#cmd("CONFIGURATION:"), the heading.],
    [#cmd("FTPD041I")], [#cmd("SRVPORT") and #cmd("SRVBIND"). #cmd("(SET AS SRVIP)") follows if the old name set the address.],
    [#cmd("FTPD042I")], [#cmd("PASVADR"), #cmd("PASVPORTS") and #cmd("PASVBIND").
      For #cmd("PASVADR=ANY") the message says #cmd("ANY (CONTROL CONNECTION)").],
    [#cmd("FTPD057I")], [#cmd("BINDTRIES") and #cmd("BINDWAIT").],
    [#cmd("FTPD043I")], [#cmd("MAXSESSIONS") and #cmd("IDLETIMEOUT").],
    [#cmd("FTPD061I")], [#cmd("MAXRETPD"), the most days that
      #cmd("SITE RETPD") takes. The message is new in FTPD 1.3.0.],
    [#cmd("FTPD044I")], [#cmd("BANNER").],
    [#cmd("FTPD049I")], [#cmd("SSLPROXY") as #cmd("YES") or #cmd("NO").],
    [#cmd("FTPD045I")], [#cmd("DEFRECFM"), #cmd("DEFLRECL") and
      #cmd("DEFBLKSIZE").],
    [#cmd("FTPD046I")], [#cmd("DEFUNIT") and #cmd("DEFVOLUME"). An empty value
      shows as #cmd("(SYSTEM CHOOSES)").],
    [#cmd("FTPD058I")], [#cmd("DEFPRIMARY"), #cmd("DEFSECONDARY"),
      #cmd("DEFSPACETYPE") as #cmd("TRK"), #cmd("CYL") or #cmd("BLK"), and
      #cmd("DEFDIRECTORY").],
    [#cmd("FTPD047I")], [#cmd("DASD VOLUMES=n:"), the number of volume lines.],
    [#cmd("FTPD048I")], [One message for each volume line, as
      #cmd("VOLSER,UNIT").],
  )
]

#cmd("CONFIG") does not show #cmd("AUTHUSER") and #cmd("JESINTERFACELEVEL").

== Stopping FTPD <ref-con-stop>

#cmd("/P FTPD") and #cmd("SHUTDOWN") start the same sequence:

+ FTPD writes #cmd("FTPD098I") and stops taking connections.
+ FTPD closes the listener and waits for the socket thread to end, at most ten
  seconds. It writes #cmd("FTPD095I WAITING FOR SOCKET THREAD TO TERMINATE (n)")
  for each second after the first that it waits, and #cmd("FTPD095W SOCKET THREAD DID NOT TERMINATE") if the wait ran out.
+ FTPD ends the worker threads and frees the trace buffer.
+ FTPD writes #cmd("FTPD099I") and the step ends. The return code is 0.

A stop during the bind retry ends the wait after at most one second. The retry
is explained in the _FTPD User's Guide_, in "When the Port Is Busy".
