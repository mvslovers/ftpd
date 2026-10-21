#import "../bookmaster/bookmaster.typ": *

= The FTPDPRM Parameters <ref-prm>

#idx("FTPDPRM", "parameters")
FTPD reads its configuration once, at start, from the DD name #cmd("FTPDPRM").
This chapter lists every parameter with its default and what FTPD does with a
value that is wrong. The _FTPD User's Guide_ explains the file and the groups
of parameters, in "Operating FTPD".

== The Rules of the File <ref-prm-rules>

- A line has the form #cmd("KEY=value"). FTPD reads a line of up to 255
  characters.
- Blanks and tabs at both ends of the key and of the value are removed. The key
  may be in any case. The value is kept as written, with the exceptions that
  the table names.
- An empty line and a line that starts with #cmd("\#") are ignored.
- FTPD tests first whether a line is a volume line (@ref-prm-dasd). No key of
  FTPD is short enough to be taken for one.
- A line that is not a volume line and has no equal sign gives the warning
  #cmd("unrecognized config line") in the log. A key that FTPD does not know
  gives #cmd("unknown config key"). The warnings go to the DD name
  #cmd("STDOUT"), not to the console. FTPD goes on.
- If a key is given twice, the last line counts.

A value that is out of range gets one of three treatments, and the tables
below say which: FTPD _keeps_ the value that was valid before (the default, or
an earlier line), FTPD _replaces_ it with a fixed value, or FTPD takes it
without a check. Only some of these cases write a warning.

== Listener and Data Connections <ref-prm-net>

#tab(caption: [Parameters of the listener and of the data connections])[
  #table(columns: (1.25in, 1.0in, 1fr),
    [Parameter], [Default], [Rules],
    [#cmd("SRVPORT")], [2121], [The port of the control connection, 1 to 65535.
      FTPD converts the value as a number. A text that is not a number, 0 and a
      number above 65535 give the warning #cmd("invalid SRVPORT") and the
      port 21.],
    [#cmd("SRVBIND")], [ANY], [The address that the control listener binds. ANY
      (in any case) is every address of the host. An address is four decimal
      numbers of up to three digits, separated by dots or by commas, each at
      most 255. Another value gives a warning and ANY.],
    [#cmd("SRVIP")], [ANY], [The old spelling of #cmd("SRVBIND"), with the same
      rules. #cmd("CONFIG") shows #cmd("(SET AS SRVIP)") if the old spelling
      set the value.],
    [#cmd("BINDTRIES")], [3], [How many times FTPD tries #cmd("bind()") for the
      control port. Below 1 FTPD uses 1, above 100 it uses 100, with a warning.
      The _FTPD User's Guide_ explains the retry, in "When the Port Is Busy".],
    [#cmd("BINDWAIT")], [5], [The seconds between two tries. The same range and
      treatment as #cmd("BINDTRIES").],
    [#cmd("PASVADR")], [ANY], [The address that FTPD sends to the client in the
      reply to #cmd("PASV"). ANY is the address that the client used for the
      control connection (the local end of it). An address has the same form as
      for #cmd("SRVBIND"). Another value gives a warning and ANY. The address
      is for the client: use it when the client has to connect to a proxy or a
      public address that the host does not carry itself.],
    [#cmd("PASVBIND")], [ANY], [The address that FTPD binds for a passive data
      connection. ANY is every address. A value that is not an address gives a
      warning and ANY.],
    [#cmd("PASVPORTS")], [22000-22200], [The range of ports for passive data
      connections, as #cmd("low-high"). FTPD converts both numbers and checks
      neither. A value without a hyphen is ignored without a message and the
      range stays. If the low number is above the high number, or no port of the
      range is free, #cmd("PASV") and #cmd("EPSV") answer #cmd("425").],
  )
]

A passive transfer takes one port of the range, from the #cmd("PASV") or
#cmd("EPSV") command until the transfer ends. A session that does not connect
within 30 seconds loses the port.

== Sessions <ref-prm-ses>

#tab(caption: [Parameters of the sessions])[
  #table(columns: (1.25in, 1.0in, 1fr),
    [Parameter], [Default], [Rules],
    [#cmd("MAXSESSIONS")], [10], [The number of sessions at the same time, 1 or
      more. A value below 1 (and a text that is not a number) gives 1, without
      a warning. FTPD checks no upper limit.],
    [#cmd("IDLETIMEOUT")], [300], [The seconds without a command after which FTPD
      sends #cmd("421") and closes the session. A value of 0 or below, or a
      text that is not a number, gives the warning #cmd("IDLETIMEOUT must be > 0") and 300.],
    [#cmd("MAXRETPD")], [365], [The most days that a client may ask for with
      #cmd("SITE RETPD"), 0 to 9999 (@ref-site-retpd-limit). The value must be
      a whole decimal number. 0 allows no retention: then only
      #cmd("SITE RETPD=0") is accepted. Another value (a negative number, a
      text, an empty value, a number with other characters after the digits,
      a number above 9999) keeps the value before and gives the warning
      #cmd("MAXRETPD must be 0..9999, keeping N"). The parameter is new in
      FTPD 1.3.0.],
    [#cmd("BANNER")], [MVS 3.8j FTPD Server], [The text of the #cmd("220") reply.
      FTPD keeps the first 79 characters.],
    [#cmd("AUTHUSER")], [empty], [FTPD stores the first eight characters. No
      function of FTPD uses the value.],
    [#cmd("SSLPROXY")], [NO], [#cmd("YES") or #cmd("NO"), in upper or in lower
      case only: another spelling, such as #cmd("Yes"), is a wrong value. With
      #cmd("YES") FTPD answers #cmd("PBSZ") and #cmd("PROT") with #cmd("200"),
      lists them in #cmd("FEAT") and writes the warnings #cmd("FTPD007W") at
      start. FTPD does not encrypt. Use it only with a TLS proxy in front of
      FTPD. A wrong value gives a warning and #cmd("NO").],
    [#cmd("JESINTERFACELEVEL")], [2], [FTPD reads a number and replaces a number
      other than 1 and 2 with 2, without a warning. No function of FTPD uses
      the value. The parameter is not a keyword of #cmd("SITE").],
  )
]

== Defaults for New Data Sets <ref-prm-def>

#idx("defaults", "new data sets")
These parameters set what a session has when it starts. A client changes them
for its own session with #cmd("SITE") (@ref-site-keys); a #cmd("SITE") keyword
with no value goes back to the value of the parameter.

#tab(caption: [Parameters for new data sets])[
  #table(columns: (1.25in, 1.0in, 1fr),
    [Parameter], [Default], [Rules],
    [#cmd("DEFRECFM")], [FB], [The record format. FTPD keeps the first three
      characters as written, in particular it does not change the case. Write
      upper case. No check.],
    [#cmd("DEFLRECL")], [80], [The record length. FTPD converts the value as a
      number and does not check it.],
    [#cmd("DEFBLKSIZE")], [3120], [The block size. FTPD converts the value as a
      number and does not check it. A value of 0 or below with
      #cmd("DEFSPACETYPE=BLOCK") gives the warning #cmd("DEFSPACETYPE=BLOCK needs a non-zero DEFBLKSIZE, using TRACK") and the unit TRACK.],
    [#cmd("DEFPRIMARY")], [10], [The primary quantity, 1 to 16777215. Another
      value keeps the value before and gives the warning #cmd("DEFPRIMARY must be 1..16777215, keeping N").],
    [#cmd("DEFSECONDARY")], [5], [The secondary quantity, 0 to 16777215. Another
      value keeps the value before and gives a warning. 0 means no secondary
      extent.],
    [#cmd("DEFDIRECTORY")], [10], [The directory blocks of a partitioned data
      set that #cmd("MKD") creates, 1 to 16777215. Another value keeps the
      value before and gives the warning #cmd("DEFDIRECTORY must be 1..16777215, keeping N"). The parameter is new in FTPD 1.3.0. Before, #cmd("MKD") used 10 unless the client sent #cmd("SITE DIRECTORY").],
    [#cmd("DEFSPACETYPE")], [TRACK], [The unit of the quantities: #cmd("TRACK"),
      #cmd("CYLINDER") or #cmd("BLOCK"). In any case; the short forms
      #cmd("TRK"), #cmd("CYL"), #cmd("BLK") and the plurals are taken as well.
      Another value keeps the value before and gives a warning.
      #cmd("CONFIG") shows the unit as #cmd("TRK"), #cmd("CYL") or
      #cmd("BLK").],
    [#cmd("DEFUNIT")], [empty], [The unit for new data sets, up to four
      characters, kept as written. Empty: the system chooses. FTPD compares it
      with the volume only if it is four digits (see below).],
    [#cmd("DEFVOLUME")], [empty], [The volume for new data sets, up to six
      characters, kept as written. Empty: the system chooses. FTPD checks it
      once at start (see below).],
  )
]

#note[*FTPD checks the placement once, at start.* At
start FTPD looks whether the volume of #cmd("DEFVOLUME") is online. If not,
FTPD writes #cmd("FTPD059W") to the console and drops the volume and the unit;
new data sets then go where the system chooses. If the volume is online and
#cmd("DEFUNIT") has four digits that do not match the device type of the
volume, FTPD writes #cmd("FTPD060W") and drops the unit. A unit that is not four
digits, for example an esoteric name, is not compared. FTPD does not check
the unit that a client sets with #cmd("SITE UNIT").]

The default space is 10 tracks as primary quantity and 5 tracks as secondary
quantity. A data set can have 16 extents (the primary and 15 secondary
extents), so the defaults give a limit of 85 tracks for one upload. An upload
that needs more ends with a #cmd("552") reply (@ref-reply-552). Raise
#cmd("DEFPRIMARY") for sites that receive large files; a client can also do it
for its session with #cmd("SITE") (see "Space for New Data Sets" in the
_FTPD User's Guide_).

== Volume Lines <ref-prm-dasd>

A line of the form

```
VOLSER,UNIT
```

names a DASD volume. The line starts with a letter. The volume serial is one to
six characters before the first comma. The unit follows the comma, up to four
characters, up to the first blank or tab. FTPD keeps up to 32 lines. A line after the 32nd gives the warning #cmd("DASD table full") in the
log. #cmd("CONFIG") shows the lines (#cmd("FTPD047I"), #cmd("FTPD048I")). No
other function of FTPD uses them.

== Errors When Reading the File <ref-prm-err>

#tab(caption: [Errors in reading FTPDPRM])[
  #table(columns: (2.0in, 1fr),
    [Case], [What FTPD does],
    [The DD name #cmd("FTPDPRM") cannot be opened], [The warning #cmd("cannot open DD:FTPDPRM, using defaults") in the log. FTPD starts with every default.],
    [The file is opened but cannot be read to its end], [#cmd("FTPD009E PARMLIB (DD:FTPDPRM) READ FAILED -- CONFIGURATION INCOMPLETE, SHUTTING DOWN").
      FTPD ends with the return code 4.],
    [A wrong value], [See the tables above.],
  )
]
