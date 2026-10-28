#import "bookmaster/bookmaster.typ": *

#show: book.with(
  title: "FTPD for MVS 3.8j",
  subtitle: "User's Guide",
  short-title: "FTPD User's Guide",
  product: "FTPD",
  number: "ML06-0001-0",
  date: "October 2026",
  authors: ("Mike Großmann",),
  edition: [
    #text(font: head-font, weight: "bold", size: 11pt)[Draft Edition (October 2026)]

    This edition applies to Version 1 Release 3 of FTPD, the FTP server for
    MVS 3.8j (ftpd 1.3.2), and to all subsequent releases and modifications
    until otherwise indicated in new editions.

    *Draft.* This book is a draft edition.

    Comments on this book may be addressed to the issue tracker of the
    mvslovers/ftpd repository on GitHub.

    © Copyright Mike Großmann 2026. All rights reserved.
  ],
)

#part("index.html", title: [FTPD User's Guide])[
#titlepage()
#contents()
#figures()

#heading(numbering: none)[About This Book] <about>

This book shows how to use FTPD (ftpd), the FTP server for MVS 3.8j, with the
command line client that comes with most systems. It is organized by tasks:
logging in, downloading and uploading data sets, working with the members of a
partitioned data set, submitting a job and fetching its output, working with
files in UFS, and finding what a typical error means. Each task has a session
that was typed at a real FTPD, so the reader sees what to type and what comes
back. The chapters for the administrator are tasks as well: installing FTPD,
setting up security, running the started task, and removing it again. Every
command, reply code, parameter and message in full is the subject of the
companion volume, the _FTPD Reference_. It also has the table of client
commands and the protocol commands they send.

The book describes FTPD 1.3: how to use it, how to install it, upgrade it and
take it off a system again, and how to run it.

== How This Book Is Organized

#deflist(width: 1.35in,
  [Chapter 1], [“Introducing FTPD”.],
  [Chapter 2], [“Connecting and Logging In”.],
  [Chapter 3], [“Working with Data Sets”.],
  [Chapter 4], [“Setting Up a Session with SITE”.],
  [Chapter 5], [“Submitting Jobs and Reading Their Output”.],
  [Chapter 6], [“Working with Files in UFS”.],
  [Chapter 7], [“Typical Errors and What They Mean”.],
  [Chapter 8], [“Setting Up Security”.],
  [Chapter 9], [“Installing FTPD”.],
  [Chapter 10], [“Running FTPD”.],
  [Chapter 11], [“Removing FTPD”.],
  [Appendix A], [“Glossary”.],
)

== Related Publications

#deflist(width: 1.35in,
  [ML06-0002], [_FTPD Reference_: the FTP commands and reply codes, the
    SITE keywords, the FTPDPRM parameters, the console commands and the
    messages of FTPD.],
  [ML02-0001], [_MBT User's Guide_: the build tool that builds FTPD and
    packages its releases.],
)

#mainmatter()
]
#set page(numbering: "1")

#part("ug-intro.html", include "guide/ug-intro.typ")
#part("ug-first.html", include "guide/ug-first.typ")
#part("ug-data.html", include "guide/ug-data.typ")
#part("ug-site.html", include "guide/ug-site.typ")
#part("ug-jes.html", include "guide/ug-jes.typ")
#part("ug-ufs.html", include "guide/ug-ufs.typ")
#part("ug-errors.html", include "guide/ug-errors.typ")
#part("ug-security.html", include "guide/ug-security.typ")
#part("ug-install.html", include "guide/ug-install.typ")
#part("ug-ops.html", include "guide/ug-ops.typ")
#part("ug-remove.html", include "guide/ug-remove.typ")

#show: appendices
#part("ug-glossary.html", include "guide/ug-glossary.typ")

#part("index-terms.html", title: [Index])[
#heading(numbering: none)[Index]
#make-index()
]
