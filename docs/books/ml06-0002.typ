#import "bookmaster/bookmaster.typ": *

#show: book.with(
  title: "FTPD for MVS 3.8j",
  subtitle: "Reference",
  short-title: "FTPD Reference",
  product: "FTPD",
  number: "ML06-0002-0",
  date: "October 2026",
  authors: ("Mike Großmann",),
  edition: [
    #text(font: head-font, weight: "bold", size: 11pt)[Draft Edition (October 2026)]

    This edition applies to Version 1 Release 3 of FTPD, the FTP server for
    MVS 3.8j (ftpd 1.3.3), and to all subsequent releases and modifications
    until otherwise indicated in new editions.

    *Draft.* This book is a draft edition.

    Comments on this book may be addressed to the issue tracker of the
    mvslovers/ftpd repository on GitHub.

    © Copyright Mike Großmann 2026. All rights reserved.
  ],
)

#part("index.html", title: [FTPD Reference])[
#titlepage()
#contents()
#figures()

#heading(numbering: none)[About This Book] <about>

This book describes FTPD (ftpd), the FTP server for MVS 3.8j, in full: the
FTP commands it answers and the reply codes it sends, the keywords of the
SITE command, the parameters of the configuration member, the operator
commands, the files of the started task and its messages. How to use them
together is the subject of the companion volume, the _FTPD User's Guide_.

== How This Book Is Organized

#deflist(width: 1.35in,
  [Chapter 1], [“FTP Commands”.],
  [Chapter 2], [“Reply Codes”.],
  [Chapter 3], [“The SITE Command”.],
  [Chapter 4], [“The FTPDPRM Parameters”.],
  [Chapter 5], [“Console Commands”.],
  [Chapter 6], [“The Started Task”.],
  [Appendix A], [“Messages”.],
)

== Related Publications

#deflist(width: 1.35in,
  [ML06-0001], [_FTPD User's Guide_: logging in, transferring data sets,
    allocation with SITE, security and operation.],
)

#mainmatter()
]
#set page(numbering: "1")

#part("ref-cmd.html", include "ref/ref-cmd.typ")
#part("ref-reply.html", include "ref/ref-reply.typ")
#part("ref-site.html", include "ref/ref-site.typ")
#part("ref-prm.html", include "ref/ref-prm.typ")
#part("ref-con.html", include "ref/ref-con.typ")
#part("ref-stc.html", include "ref/ref-stc.typ")

#show: appendices
#part("apx-messages.html", include "ref/apx-messages.typ")

#part("index-terms.html", title: [Index])[
#heading(numbering: none)[Index]
#make-index()
]
