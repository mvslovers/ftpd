#import "../bookmaster/bookmaster.typ": *

= Setting Up Security <ug-security>

#idx("security")#idx("RAKF")
FTPD has no user database of its own. RAKF verifies every user ID and
password, and RAKF decides every access to a data set. Without RAKF nobody can
log in, and no parameter turns that off. This chapter says what FTPD asks RAKF,
what a client sees when RAKF says no, which identity the started task
itself has, and how an administrator lets users see the jobs of others. The tables, the profiles and the steps to change them are in
`docs/FTPD_RAKF_SETUP.md` in the FTPD (ftpd) repository. This book does not
repeat them.

#note[*Do not edit #cmd("PROFILES") by hand; RAKF's maintainer recommends the commands.* Create profiles with #cmd("RDEFINE"), #cmd("PERMIT") and #cmd("ADDSD"), then reload with #cmd("S RAKFPROF"). A hand-edited file that is wrong aborts the reload and, on RAKF 2.0.0, keeps JES2 from starting at the next IPL.]

== Checking How Users Log In <ug-security-login>

#idx("login")#idx("password")
A session ends in the state "logged in" only after RAKF has accepted the user
ID and the password and the user may use FTP (@ug-security-ftpauth). FTPD
folds both to upper case before it asks RAKF, so the case of the password
does not matter. A password longer than eight characters is cut to eight.

A client has three tries:

- *RAKF refuses the user ID or the password:* #cmd("530 Login incorrect.")
  and the client may start again with #cmd("USER").
- *The third try fails:* #cmd("530 Login incorrect. Too many attempts, disconnecting.") and FTPD closes the control connection.
- *The login works:* #cmd("230 USERID is logged on.  Working directory is \"USERID.\".") The working directory is the user ID and a period
  (@ug-data-cwd).

The count is per connection. It starts again at zero after a successful
login. The replies are the same for an unknown user and for a wrong password,
so a client cannot find out which user IDs exist.

Before the login FTPD answers only a short list of commands. The list is
#cmd("USER"), #cmd("PASS"), #cmd("QUIT"), #cmd("SYST"), #cmd("FEAT"),
#cmd("HELP"), #cmd("NOOP") and #cmd("STAT"). #cmd("AUTH") is answered with
#cmd("504 Security mechanism not implemented."). When the parameter
#cmd("SSLPROXY") is #cmd("YES"), #cmd("PBSZ") and #cmd("PROT") are answered as
well. Every other command gets #cmd("530 Not logged in.").

#note[*FTPD does not encrypt.* The user ID, the password and the data cross the
network as they are. FTPD does not do TLS itself. With #cmd("SSLPROXY=YES") it
accepts #cmd("PBSZ") and #cmd("PROT") from a client that talks to a TLS proxy
in front of FTPD (@ug-ops-config). It does not check that a proxy exists.]

== Allowing a User to Use FTP: FTPAUTH <ug-security-ftpauth>

#idx("FTPAUTH")#idx("FACILITY class")
After the password is right, FTPD asks RAKF whether the user has READ access to
the resource #cmd("FTPAUTH") in the class #cmd("FACILITY"). If not, the answer
is the same as for a wrong password, with another text:

```
530 Not authorized for FTP access.
```

The third refusal ends the connection: #cmd("530 Not authorized for FTP access. Disconnecting."). A refused attempt counts like a wrong password.

#note[*Define the profile.* When no profile covers #cmd("FTPAUTH"), RAKF
answers that the resource is not protected, and FTPD lets the user in. This is
not a safe default. RAKF gives ALTER access to resources that have no profile,
so every user who knows a password can use FTP. Create it with
#cmd("RDEFINE") and #cmd("PERMIT") (@ug-install-rakfsetup).]

== Controlling Access to Data Sets <ug-security-dsn>

#idx("data set", "access")#idx("DATASET class")
For every command that touches a data set, FTPD asks RAKF whether the
_logged-in user_ may do it. The class is #cmd("DATASET"). FTPD does not use the
identity of the started task for this check.

#tab(caption: [The access that each command needs])[
  #table(columns: (1.6in, 1fr),
    [Command], [Access],
    [#cmd("RETR")], [READ],
    [#cmd("LIST") and #cmd("NLST") of the members of a partitioned data set],
      [READ],
    [#cmd("STOR"), #cmd("APPE")], [UPDATE],
    [#cmd("DELE"), #cmd("MKD"), #cmd("RMD")], [ALTER],
    [#cmd("RNFR"); #cmd("RNTO") for the new name too], [ALTER],
  )
]

A refusal is answered with the name of the data set:

```
550 Access denied to IBMUSER.PAYROLL.DATA
```

When no profile covers the data set, RAKF says so, and FTPD allows the access.
This is the answer that data set OPEN, IDCAMS and the catalog accept as well. A
system without #cmd("DATASET") profiles lets every user do everything with
every data set, and FTPD only shows that.

#note[*A list of data sets shows names, not contents.* A catalog list
(#cmd("LIST") without a partitioned data set as working directory) returns
every name that fits the name or pattern, also names of data sets that the user
may not read. The profile protects the contents: #cmd("RETR") of such a data
set is refused with #cmd("550"). This is how the catalog works on MVS, and the
same is true for #cmd("LISTCAT") in TSO.]

== Giving the Started Task Its Identity <ug-security-stc>

#idx("started task", "identity")#idx("FTPD004I")#idx("FTPD004W")
The started task is not a user, and it has an identity of its own: the one that
RAKF gives it when the system starts it. FTPD uses this identity for what the
started task itself does. It puts it back after every operation that runs
under a user's identity and after every ABEND that it recovers from.

Since FTPD 1.3.0 FTPD takes the identity as RAKF gave it. It reports the
identity at startup:

```
FTPD004I STC RUNS AS STC/STCGROUP
```

Earlier releases logged on as the user #cmd("FTPD") in the group #cmd("USER") at
startup. FTPD 1.3 does not, and the line for #cmd("FTPD") in the RAKF table
of users is no longer used by FTPD.

What the message shows depends on the RAKF release:

- *RAKF maps the name of the started task to an identity:* the message shows
  that identity. This is what you want, with an identity that has no
  Operations authority. RAKF 2.0.0 does this: the identity is the user ID in
  the table of users that is named like the started procedure,
  #cmd("FTPD"), with the group and the Operations flag of its entry.
  @ug-install-rakfsetup says what the group of that user needs.
- *RAKF has no such mapping:* the message shows #cmd("STC/STCGROUP"), the
  identity that RAKF gives every started task. #cmd("STC") has Operations
  authority. This is the case on TK5 before Update 6 and on RAKF 1.2.x. On
  RAKF 2.0.0 it is the case when the table of users has no user ID
  #cmd("FTPD").
- *The address space has no security environment:* the message is
  #cmd("FTPD004W NO SECURITY ENVIRONMENT FOR THE STC: RAKF PERMITS ANY ACCESS WITHOUT AN ACEE"). RAKF treats the missing environment as "access permitted",
  so the checks that do not run in a user's session permit everything. Do not
  start FTPD like this in production.

#note[*The identity of the started task has to stay least-privilege.* Sessions
run in parallel and share one security field of the address space. When a
session ends in an ABEND, or when two operations meet, a concurrent session
can for a moment run under the identity of the started task. With a low
identity such a session can only lose authority. With #cmd("STC/STCGROUP")
it could gain authority for the moment. So do not give the started task
access to data sets, and map it to a low identity if your RAKF can. Read the
message at every start.]

== Checking the Jobs That Users Submit <ug-security-jes>

#idx("JES", "security")
When a client stores a job in the JES view (@ug-jes), FTPD adds the
user ID and the password of the session to the JOB statement
(#cmd("USER=") and #cmd("PASSWORD=")), so the job runs as that user and not as
the started task. FTPD keeps the password in the memory of the session for
this. FTPD looks for the JOB statement, adds #cmd("NOTIFY=DUMMY") when the
statement has no #cmd("NOTIFY"), and does not look at the rest of the JCL. It
adds the two operands only when it finds a JOB statement. What the job may do is what
RAKF allows this user.

Jobs of other users are a separate matter: FTPD asks RAKF for each one
(@ug-security-jesspool).

== Letting Users See the Jobs of Others: JESSPOOL <ug-security-jesspool>

#idx("JESSPOOL")#idx("JES", "security")#idx("RAKF", "JESSPOOL")
This section needs RAKF 2.0.0 or later, the release whose class table contains
#cmd("JESSPOOL"). FTPD 1.3 does not need that release to run: on a RAKF 1.2.x
it installs and starts, and every user sees, reads and cancels only the own
jobs.

In the JES view (@ug-jes) FTPD decides for every job whether the user may list
it, read its output and cancel it. A user always works on the own jobs. For a
job of another user FTPD asks RAKF, in the class #cmd("JESSPOOL"), for READ
access, and for a #cmd("delete") for UPDATE access. The entity is the owner, the
job name and the job ID, separated by dots:

```
ALICE.PAYROLL.JOB00038
```

A job without an owner has the entity #cmd(".PAYROLL.JOB00038"). The owner comes
first because RAKF compares only the first eight characters of an entity, so a
profile #cmd("ALICE.*") is a rule about the user ALICE.

When no profile covers the entity, or the profile gives #cmd("NONE"), the job
is hidden: it is not in the list, and #cmd("get"), #cmd("delete") and
#cmd("dir JOBnnnnn") answer #cmd("550 Jobid ... not found ..."), the reply for
a job that does not exist. #cmd("READ") lists and reads the job, and a
#cmd("delete") gets #cmd("550 Access denied - cannot cancel JOBnnnnn").
#cmd("UPDATE"), #cmd("CONTROL") and #cmd("ALTER") allow everything
(@ug-jes-others has the sessions). #cmd("site jesowner=*") and
#cmd("site jesowner=USERID") need READ on the jobs they would show: the check
for #cmd("*") asks for the entity #cmd("..") and the check for another user ID
asks for #cmd("USERID.."). The profile #cmd("*") covers both, and the profile
#cmd("USERID.*") covers the second.

*Without any profile every user sees only the own jobs.* That is the state
after the upgrade to FTPD 1.3, and it is the most restrictive one. FTPD 1.2.0
let every user list, read and cancel the jobs of everybody. An administrator
who relied on that creates the profiles before the upgrade.

=== Creating the Profiles

#note[*Do not edit #cmd("PROFILES") by hand; RAKF's maintainer recommends the commands.* Create profiles with #cmd("RDEFINE"), #cmd("PERMIT") and #cmd("ADDSD"), then reload with #cmd("S RAKFPROF"). A hand-edited file that is wrong aborts the reload and, on RAKF 2.0.0, keeps JES2 from starting at the next IPL.]

The profiles are lines in #cmd("SYS1.SECURE.CNTL(PROFILES)"). Create them with
the RAKF commands #cmd("RDEFINE") (#cmd("RDEF")) and #cmd("PERMIT")
(#cmd("PE")), and reload the tables with the procedure #cmd("RAKFPROF"). The
commands come with RAKF 2.0.0 and run in a TSO batch job. The user of the job
must be connected to the group #cmd("RAKFADM") and have the SPECIAL authority
(@ug-install-rakfsetup says how to give it). The job below sets this up:

- Everyone sees only the own jobs (#cmd("*") with #cmd("NONE")).
- The group #cmd("AUDIT") reads the jobs of all users (#cmd("PE") with
  #cmd("READ")).
- The started tasks stay closed to everyone (#cmd("SYSTEM.*") with
  #cmd("NONE")).

#screen(```
//JESSPOOL JOB (ACCT),'JESSPOOL',CLASS=A,MSGCLASS=H,GROUP=RAKFADM
//TSO      EXEC PGM=IKJEFT01,REGION=4096K
//SYSPRINT DD SYSOUT=*
//SYSTSPRT DD SYSOUT=*
//SYSTSIN  DD *
RDEF * CL(JESSPOOL) UACC(NONE)
PE * CL(JESSPOOL) ID(AUDIT) ACCESS(READ)
RDEF SYSTEM.* CL(JESSPOOL) UACC(NONE)
/*
//RAKFPROF EXEC RAKFPROF
```)

The first command defines the universal access of the profile #cmd("*"), and
the second adds the group. The output of the step shows the answers of the
second and the third command:

#screen(raw(read("../ex/ug-security/js-profile.txt"), block: true))

#cmd("RAKFPROF") reloads the tables, and the job log says
#cmd("RAKFPROF7  RAKF PROFILES UPDATED"). Without the reload the profiles are
not in effect. A change applies from the next command of a session that is
running, and a change of the groups of a user applies at the next login.

The group #cmd("ADMIN") of MVS/CE gets the right to cancel jobs in the same
way, with one more command and the reload:

#screen(raw(read("../ex/ug-security/js-admin.txt"), block: true))

After the two jobs #cmd("SYS1.SECURE.CNTL(PROFILES)") has these lines for the
class #cmd("JESSPOOL"). The lines are column-positioned: the class in columns 1 to 8, the entity from column 9,
the group in columns 53 to 60 and the access from column 61
(@ug-install-rakfsetup):

#screen(raw(read("../ex/ug-security/js-lines.txt"), block: true))

Everybody sees the own jobs, #cmd("AUDIT") reads all jobs, #cmd("ADMIN") reads
and cancels all jobs, and the jobs of the started tasks, which have the owner
#cmd("SYSTEM"), are closed to all of them. The sessions in @ug-jes-others ran
against these profiles.

=== Rules for the Profiles

The setup guide (`docs/FTPD_RAKF_SETUP.md`, section 7) has all the rules and
more examples. The ones that decide most cases are:

- *Write an owner profile with a star:* #cmd("ALICE.*"), never #cmd("ALICE").
  Write at most one #cmd("*") profile.
- *The last matching profile wins, not the longest.* RAKF orders the profiles
  by the EBCDIC collating sequence, and #cmd("*") sorts after #cmd("."). A
  profile for an owner therefore beats #cmd("*") for the jobs of that owner.
  The exceptions are the owners that sort before #cmd("*"), such as a user ID
  that starts with #cmd("$").
- *Write #cmd("SYSTEM.*") with #cmd("NONE")* as soon as a universal line gives
  READ. Otherwise the logs of all started tasks, those of FTPD included, are
  open to every reader. Nothing enforces this line.
- *Every entity with a group line needs a universal line:* #cmd("*"),
  #cmd("SYSTEM.*") and #cmd("ALICE.*") each. #cmd("PE") refuses a profile
  without one.
- *Put the line of the administrators into every owner block.* A block
  without it falls back to its universal access, and as the last matching
  profile it wins against #cmd("*").
- *One connected group is enough.* The right comes from any group the user is
  connected to in #cmd("USERS"). The authority OPERATIONS grants nothing in the
  class #cmd("JESSPOOL").
- *The prefix trap:* the profile #cmd("MIKE1.*") does not cover the jobs of
  #cmd("MIKE10").

#note[*What the profiles do not do.* They never restrict the own jobs of a
user, so a user ID that must not use the spool is kept out at the login
(@ug-security-ftpauth). They regulate FTP only, not TSO, SDSF or any other
interface. And the owner of a job is what the submitter wrote into the job, so
the view of the spool in FTP is no audit trail.]
