#!/usr/bin/env python3
"""Reject writable file-scope data in the AC(1) load modules.

Why this exists (issue #101)
----------------------------
FTPD is link-edited AC(1).  Fetched from an APF-authorized library, the job
step is authorized before program fetch runs, and MVS then obtains the job
pack area in subpool 252 with **storage key 0** so that problem-key code
cannot patch authorized code.  The STC itself runs problem state key 8.
Every store into the module's own storage -- i.e. every write to a C static
or a non-const global -- therefore takes a protection exception.

FTPD's own was the first line of main(), `ftpd_server = &server`, ahead of
the first WTO.  The entire JESMSGLG of a start on an APF-authorized
FTPD.LINKLIB was one line:

    12.34.30 STC 1124  IEF450I FTPD FTPD - ABEND S0C4 U0000 - TIME=12.34.30

Without APF the same module is fetched key 8 and the store goes through
unnoticed, which is why this only shows up on hardened systems.  Authorizing
ourselves later via SVC 244 (clib_apf_setup) does not change it either way:
that sets JSCBAUTH, it cannot relabel storage program fetch already
allocated.

There is a second reason, independent of authorization: ld370 marks a load
module RENT and REUS unless the module opts out with `norent` / `noreus`, and
FTPD does not.  Writable module data breaks that promise outright, and would
fail the same way in the (key 0, page-protected) LPA.

So: no mutable file-scope data in an AC(1) module.  Put the value in
ftpd_server (a main() local, key 8, published process-wide through the GRT --
see ftpd_log_anchor() for the pattern), on the heap, or in CSA behind the
usual key-0 window.  Making it `const` is the other answer, and the right one
for tables that are only ever read.

An absent `ac` counts as 0, matching mbt -- see mbtconfig.py,
`mod.get("ac", 0)` -- so an AC(0) module is not checked: the job step is
never authorized, and MVS fetches it key 8.

Scope and limits
----------------
Only `[[module]]` sources are checked.  ufsd's `client/libufs.c` is linked
into FTPD and is not covered here (different repo); scanned with this same
detector for #101, it has no module-resident data at all.

libc370 is out of reach too.  Its module-resident statics are real, but the
ones on FTPD's paths are no-CRT fallbacks -- `errno` resolves through
`crt->crterrno` whenever a CLIBCRT exists, which under the STC it always
does.

This is a text proxy, not the check.  It cannot tell a `const` pointer from a
pointer to const, and it does not see assembler.  The precise cross-check is
to compile with `cc370 -S` and look for a store through a register loaded
from `=A(@Vn)` or an X-var -- that is what found the offenders in the first
place, and what confirmed the fix.

Ecosystem note: this file is a copy of ufsd's (mvslovers/ufsd#66), which found
FTPD's bug.  Keep them in step until mbt grows a shared home for it.

Usage: tools/check-module-data.py [project.toml]
"""

import glob
import os
import re
import sys

try:
    import tomllib
except ImportError:                                     # Python < 3.11
    sys.exit("check-module-data: needs Python 3.11+ (tomllib)")


def strip_noise(src):
    """Blank out comments, string/char literals and preprocessor lines.

    Newlines are preserved so reported line numbers stay usable.
    """
    out = []
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c == '/' and src[i:i + 2] == '/*':
            j = src.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append(''.join(ch if ch == '\n' else ' ' for ch in src[i:j]))
            i = j
            continue
        if c == '/' and src[i:i + 2] == '//':
            j = src.find('\n', i)
            j = n if j < 0 else j
            out.append(' ' * (j - i))
            i = j
            continue
        if c in '"\'':
            quote, j = c, i + 1
            while j < n and src[j] != quote:
                j += 2 if src[j] == '\\' else 1
            out.append('""' + ' ' * max(0, j - i - 1))
            i = j + 1
            continue
        out.append(c)
        i += 1
    text = ''.join(out)
    return '\n'.join('' if l.lstrip().startswith('#') else l
                     for l in text.split('\n'))


def declarations(src):
    """Yield (line, text, depth) for every statement, file scope and inside
    function bodies alike -- a function-local `static` is module storage too.

    A '{' right after '=' or ',' opens an initializer, not a block.  The
    declarator that closes a `typedef struct { ... } NAME;` is a type name, not
    data -- but `struct tag { ... } instance;` is data, so only typedef tails
    are dropped.
    """
    depth, init, buf, line, start = 0, 0, '', 1, 1
    heads, typetail = [], False
    for ch in src:
        if ch == '\n':
            line += 1
        if ch == '{':
            if init or buf.rstrip().endswith(('=', ',')):
                init += 1               # aggregate initializer, keep reading
                buf += ' '
                continue
            depth += 1
            heads.append(' '.join(buf.split()))
            buf, start = '', line
            continue
        if ch == '}':
            if init:
                init -= 1
                buf += ' '
                continue
            depth = max(0, depth - 1)
            typetail = bool(re.search(r'\btypedef\b',
                                      heads.pop() if heads else ''))
            buf, start = '', line
            continue
        if ch == ';' and not init:
            head = ' '.join(buf.split())
            if head and not typetail:
                yield start, head, depth
            typetail = False
            buf, start = '', line
            continue
        if not buf.strip():
            start = line
        buf += ch


IS_FUNC = re.compile(r'\([^)]*\)\s*$')
IS_FUNC_PTR = re.compile(r'\(\s*\*')
SKIPPABLE = re.compile(r'\b(typedef|extern)\b')
IS_CONST = re.compile(r'\bconst\b')
IS_TAG_ONLY = re.compile(r'^(struct|union|enum)\s+\w+$')


def mutable(head, depth):
    if SKIPPABLE.search(head) or IS_CONST.search(head):
        return False
    if depth and not head.startswith('static'):
        return False        # an ordinary local lives on the stack
    if IS_TAG_ONLY.match(head):
        return False
    if IS_FUNC.search(head) and not IS_FUNC_PTR.search(head):
        return False        # prototype or definition head
    return bool(re.search(r'\w', head))


def sources_of(module, root):
    """The module's C sources.  Hand-written assembler is not parsed here --
    its storage is explicit, and `DS`/`DC` in a CSECT is visible on sight."""
    files = []
    for pattern in module.get('sources', []):
        files += glob.glob(os.path.join(root, pattern))
    dropped = set()
    for pattern in module.get('exclude', []):
        dropped |= set(glob.glob(os.path.join(root, pattern)))
    return sorted(f for f in set(files) - dropped if f.endswith('.c'))


def main(argv):
    toml = argv[1] if len(argv) > 1 else 'project.toml'
    root = os.path.dirname(os.path.abspath(toml)) or '.'
    with open(toml, 'rb') as fh:
        project = tomllib.load(fh)

    findings = []
    checked = 0
    for module in project.get('module', []):
        if module.get('ac', 0) != 1:
            continue
        for path in sources_of(module, root):
            checked += 1
            src = strip_noise(open(path, encoding='utf-8',
                                   errors='replace').read())
            for line, head, depth in declarations(src):
                if mutable(head, depth):
                    findings.append((module['name'],
                                     os.path.relpath(path, root), line, head))

    if not findings:
        print(f"check-module-data: {checked} sources clean "
              f"(no writable file-scope data in AC(1) modules)")
        return 0

    print("check-module-data: writable file-scope data in an AC(1) module\n")
    for name, path, line, head in findings:
        print(f"  {path}:{line}: {head[:100]}   [{name}]")
    print("\nFetched from an APF-authorized library these modules land in "
          "key-0 storage;\na key-8 store into them abends S0C4 (issue #101). "
          "Move the value into\nftpd_server (published through the GRT -- see "
          "ftpd_log_anchor), onto the\nheap, or into CSA behind a key-0 window "
          "-- or make it const.")
    return 1


if __name__ == '__main__':
    sys.exit(main(sys.argv))
