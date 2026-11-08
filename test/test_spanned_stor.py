#!/usr/bin/env python3
"""STOR into spanned data sets: is every record on disk? (ftpd#430, libc370#757)

A record of a spanned data set (RECFM=VBS, or RECFM=VB whose BLKSIZE is below
LRECL+4, which libc370's OPEN exit turns into VBS) that does not fit the rest
of the block is written as several segments.  libc370 up to 2.28.0 (measured
with 2.25.1; the cause is libc370#757, fixed in 2.29.1) flags the first
segment as a last one and loses the tail, and fwrite()/fflush() still report
success, so FTPD's check of ferror() in stor_put() sees nothing.  With 2.29.1
and later every case below is complete.

For each target and each transfer mode this stores data whose records
cross block boundaries, among them records that span more than two blocks
(text: lines of 590 and 592 bytes in 251-byte blocks; binary: LRECL-sized
chunks), copies the blocks to a RECFM=U data set (IEBGENER, so no access
method interprets the segments), reads that back over FTP and rebuilds the
records from BDW, RDW and segment flags.  A case passes when the rebuilt
records are what was sent (binary mode: the bytes sent, since FTPD re-wraps
a byte stream into records of its own).  A reply of 4xx/5xx is a loud
failure, a 250 with missing data a silent loss.

Exit status: 0 every case complete, 1 data lost, 2 the test could not run.

The FTP side needs a data connection the client can reach: a container's
FTPD answers PASV with 127,0,0,1, so run the script where that address is the
container's own.  The JCL steps (allocation of the VBS targets, the read-back
copy) go through mvsMF, taken from --mvsmf/--mvsmf-user/--mvsmf-password or
MBT_TARGET_MVSMF_URL/_USER/_PASSWORD.

    test_spanned_stor.py [--host H] [--port P] [--user U] [--pass PW]
                         [--mvsmf URL] [--unit SYSDA] [--volume VOL]
                         [--keep] [--json FILE] [--only VBS|VB|BIG|LIMIT|LIMITVB]...

Targets: LIMIT = the longest text line of RECFM=VBS 600/259: 592 data bytes
(LRECL - 8) are stored, 593 and 596 are refused with a 451 that names the
limit (no ABEND), and with SITE TRUNCATE a line of 596 is cut to 592 (ftpd#432);
LIMITVB = the same for VB 600/259 allocated by FTPD (OPEN makes it VBS: 592,
593 and 596 as for LIMIT, checked on the replies and the data set's attributes
only), and VB 600/6233, which stays unspanned: 596 data bytes are stored and
read back, 597 are refused (ftpd#432);
VBS = RECFM=VBS 600/259 allocated by JCL (an existing data set);
VB = RECFM=VB 600/259 allocated by FTPD through SITE (LRECL+4 > BLKSIZE, so
OPEN makes it VBS); BIG = RECFM=VBS 255/6233 by JCL, 200-byte records that
cross block boundaries.
"""
import argparse
import base64
import ftplib
import io
import json
import os
import re
import sys
import time
import urllib.error
import urllib.request

LRECL = 600
BLKSIZE = 259        # < LRECL+4: spanned; 251 bytes of data per block
BIG_LRECL = 255
BIG_BLKSIZE = 6233   # records of 200 bytes: the 32nd does not fit block 1

# Text lines: some cross a block boundary, 590 and 592 span three blocks.
TEXT_LENGTHS = [79, 80, 81, 10, 590, 3, 247, 248, 251, 252, 255, 592, 1, 1]
BIG_TEXT_LENGTHS = [200] * 70

# (tag, kind, recfm, lrecl, blksize, mode)
# kind VBS: allocated by JCL; kind VB: allocated by FTPD (SITE RECFM=VB).
CASES = [
    ("VBS", "VBS", LRECL, BLKSIZE),
    ("VB", "VB", LRECL, BLKSIZE),
    ("BIG", "VBS", BIG_LRECL, BIG_BLKSIZE),
]


def text_lines(lengths):
    out = []
    for i, n in enumerate(lengths):
        s = "L%02d:" % i + "".join(chr(65 + j % 26) for j in range(n))
        out.append(s[:n])
    return out


def text_payload(lengths):
    lines = text_lines(lengths)
    sent = ("\n".join(lines) + "\n").encode("ascii")
    expected = [(ln if ln else " ").encode("cp037") for ln in lines]
    return sent, expected


def binary_payload(lrecl, nrec):
    """nrec records of LRECL bytes and a partial one, which FTPD pads."""
    data = b"".join(bytes((r * 31 + j) % 256 for j in range(lrecl))
                    for r in range(nrec))
    return data + bytes((j * 7 + 1) % 256 for j in range(100)), None


class Mvsmf:
    def __init__(self, url, user, password):
        self.url = url.rstrip("/")
        self.auth = "Basic " + base64.b64encode(
            ("%s:%s" % (user, password)).encode()).decode()

    def req(self, method, path, data=None, headers=None):
        h = {"Authorization": self.auth, "X-CSRF-ZOSMF-HEADER": "x"}
        h.update(headers or {})
        r = urllib.request.Request(self.url + "/zosmf" + path, data=data,
                                   method=method, headers=h)
        try:
            with urllib.request.urlopen(r, timeout=120) as f:
                return f.status, f.read()
        except urllib.error.HTTPError as e:
            return e.code, e.read()

    def run(self, jcl, tmo=300):
        s, b = self.req("PUT", "/restjobs/jobs", jcl.encode(),
                        {"Content-Type": "text/plain"})
        if s >= 300:
            raise RuntimeError("job submit: HTTP %d %r" % (s, b[:200]))
        j = json.loads(b)
        name, jid = j["jobname"], j["jobid"]
        t0 = time.time()
        while time.time() - t0 < tmo:
            s, b = self.req("GET", "/restjobs/jobs/%s/%s" % (name, jid))
            st = json.loads(b)
            if st.get("status") == "OUTPUT":
                return jid, str(st.get("retcode"))
            time.sleep(2)
        raise RuntimeError("job %s did not end in %ds" % (jid, tmo))


def ftp_login(a):
    f = ftplib.FTP()
    f.connect(a.host, a.port, timeout=120)
    f.login(a.user, a.password)
    return f


def reply_code(text):
    m = re.match(r"(\d{3})", text.strip().splitlines()[-1])
    return m.group(1) if m else "???"


def stor(f, mode, dsn, payload):
    """STOR payload; text goes through storlines(), which sends TYPE A
    (storbinary() always switches to TYPE I) and CRLF line ends."""
    try:
        if mode == "text":
            r = f.storlines("STOR '%s'" % dsn, io.BytesIO(payload))
        else:
            f.voidcmd("TYPE I")
            r = f.storbinary("STOR '%s'" % dsn, io.BytesIO(payload))
        return reply_code(r), r.splitlines()[-1]
    except (ftplib.error_perm, ftplib.error_temp) as e:
        return reply_code(str(e)), str(e)


def list_attrs(f, dsn):
    lines = []
    try:
        f.retrlines("LIST '%s'" % dsn, lines.append)
    except ftplib.all_errors as e:
        return "LIST failed: %s" % e
    for ln in lines:
        p = ln.split()
        if p and p[-1].strip("'").upper().endswith(dsn.split(".", 1)[-1].upper()):
            return "%s %s %s" % (p[5], p[6], p[7]) if len(p) >= 8 else ln
    return "?"


def delete(f, dsn):
    try:
        f.delete("'%s'" % dsn)
    except ftplib.all_errors:
        pass


def parse_blocks(data):
    """Rebuild records from RECFM=U blocks that hold BDW + RDW segments.

    Returns (records, anomalies, blocks); blocks is [(bdw_len, [(rdw_len, flag)])].
    """
    recs, anomalies, blocks = [], [], []
    cur = None
    pos = 0
    while pos < len(data):
        if len(data) - pos < 4:
            anomalies.append("trailing %d bytes after the last block" % (len(data) - pos))
            break
        bdw = int.from_bytes(data[pos:pos + 2], "big")
        if bdw < 8 or pos + bdw > len(data):
            anomalies.append("bad BDW %d at offset %d" % (bdw, pos))
            break
        end, p, segs = pos + bdw, pos + 4, []
        while p < end:
            if end - p < 4:
                anomalies.append("short RDW at offset %d" % p)
                break
            rdw = int.from_bytes(data[p:p + 2], "big")
            flag = data[p + 2]
            if rdw < 4 or p + rdw > end:
                anomalies.append("bad RDW %d at offset %d" % (rdw, p))
                break
            body = data[p + 4:p + rdw]
            segs.append((rdw, flag))
            if flag == 0:
                if cur is not None:
                    anomalies.append("complete record inside an unfinished one at offset %d" % p)
                    cur = None
                recs.append(body)
            elif flag == 1:
                if cur is not None:
                    anomalies.append("first segment inside an unfinished record at offset %d" % p)
                cur = bytearray(body)
            elif flag == 3:
                if cur is None:
                    anomalies.append("middle segment without a first one at offset %d" % p)
                else:
                    cur += body
            elif flag == 2:
                if cur is None:
                    anomalies.append("last segment without a first one at offset %d" % p)
                    recs.append(body)
                else:
                    recs.append(bytes(cur + body))
                    cur = None
            else:
                anomalies.append("bad segment flag %02X at offset %d" % (flag, p))
            p += rdw
        blocks.append((bdw, segs))
        pos = end
    if cur is not None:
        anomalies.append("record unfinished at the end of the data")
    return recs, anomalies, blocks


def compare_stream(payload, got, lrecl):
    """Binary mode is a byte stream: FTPD re-wraps it into records of its own
    and pads the last one with X'00' to LRECL, so the record boundaries are
    not the sender's.  Complete = the payload, then at most LRECL-1 zeros."""
    data = b"".join(got)
    if data.startswith(payload) and len(data) - len(payload) < lrecl \
            and not any(data[len(payload):]):
        return True, "all %d bytes (+%d padding) in %d records" % (
            len(payload), len(data) - len(payload), len(got))
    n = 0
    while n < min(len(data), len(payload)) and data[n] == payload[n]:
        n += 1
    return False, "%d of %d bytes in %d records; first difference at byte %d" % (
        min(n, len(payload)), len(payload), len(got), n)


def compare(expected, got):
    """(complete, text) for two lists of records."""
    if expected == got:
        return True, "all %d records, %d bytes" % (len(got), sum(map(len, got)))
    n = 0
    while n < min(len(expected), len(got)) and expected[n] == got[n]:
        n += 1
    msg = "%d of %d records, %d of %d bytes; first difference in record %d" % (
        sum(1 for a, b in zip(expected, got) if a == b), len(expected),
        sum(map(len, got)), sum(map(len, expected)), n + 1)
    if n < len(got) and n < len(expected):
        msg += " (expected %d bytes, found %d)" % (len(expected[n]), len(got[n]))
    return False, msg


def _dd(name, dsn, disp, unit, volume, dcb, space=True):
    cards = ["//%-8s DD DSN=%s," % (name, dsn), "//            DISP=%s," % disp]
    if unit:
        cards.append("//            UNIT=%s," % unit)
        if volume:
            cards.append("//            VOL=SER=%s," % volume)
        if space:
            cards.append("//            SPACE=(TRK,(10,10)),")
    cards.append("//            DCB=(%s)" % dcb)
    return "\n".join(cards)


def readback_jcl(unit, volume, src, dst, blksz):
    dcb = "RECFM=U,BLKSIZE=%d" % blksz
    return "\n".join([
        "//KAIRB    JOB (ACCT),'SPANNED RB',CLASS=A,MSGCLASS=H",
        "//DEL      EXEC PGM=IEFBR14",
        "//D1       DD DSN=%s,DISP=(MOD,DELETE)," % dst,
        "//            UNIT=%s,SPACE=(TRK,1)" % unit,
        "//GEN      EXEC PGM=IEBGENER",
        "//SYSPRINT DD SYSOUT=*",
        "//SYSIN    DD DUMMY",
        _dd("SYSUT1", src, "SHR", None, None, dcb),
        _dd("SYSUT2", dst, "(NEW,CATLG)", unit, volume, dcb),
        ""])


def alloc_jcl(unit, volume, dsn, lrecl, blksz):
    dcb = "RECFM=VBS,LRECL=%d,BLKSIZE=%d" % (lrecl, blksz)
    return "\n".join([
        "//KAIAL    JOB (ACCT),'SPANNED ALLOC',CLASS=A,MSGCLASS=H",
        "//S1       EXEC PGM=IEFBR14",
        _dd("D1", dsn, "(NEW,CATLG)", unit, volume, dcb),
        ""])


def run_case(a, mvsmf, hlq, tag, kind, lrecl, blksz, mode):
    sfx = "T" if mode == "text" else "B"
    dsn = "%s.TEST.SP%s%s" % (hlq, tag, sfx)
    rb = "%s.TEST.SPRB%s" % (hlq, sfx)
    row = dict(target="%s %d/%d" % (kind, lrecl, blksz), mode=mode, dsn=dsn)

    if mode == "text":
        lengths = BIG_TEXT_LENGTHS if tag == "BIG" else TEXT_LENGTHS
        payload, expected = text_payload(lengths)
    else:
        payload, expected = binary_payload(lrecl, 8)
    row["sent_records"] = len(expected) if expected else None
    row["sent_bytes"] = len(payload)

    f = ftp_login(a)
    try:
        delete(f, dsn)
        delete(f, rb)
        if kind == "VBS":
            jid, rc = mvsmf.run(alloc_jcl(a.unit, a.volume, dsn, lrecl, blksz))
            row["alloc_job"] = "%s %s" % (jid, rc)
            if not rc.endswith("0000"):
                raise RuntimeError("allocation job %s ended %s" % (jid, rc))
        else:
            for c in ("SITE RECFM=VB", "SITE LRECL=%d" % lrecl,
                      "SITE BLKSIZE=%d" % blksz, "SITE TRACKS",
                      "SITE PRIMARY=10", "SITE SECONDARY=10"):
                r = f.sendcmd(c)
                if not r.startswith("200"):
                    raise RuntimeError("%s: %s" % (c, r))
                row.setdefault("site_replies", []).append(r)
        row["attrs_before"] = list_attrs(f, dsn) if kind == "VBS" else "(not yet allocated)"

        row["stor_code"], row["stor_reply"] = stor(f, mode, dsn, payload)
        row["attrs_after"] = list_attrs(f, dsn)
        f.close()
        if row["attrs_after"].startswith(("?", "LIST failed")):
            row["complete"] = False
            row["anomalies"] = []
            row["detail"] = "no data set after the STOR (FTPD scratched it)"
            return row

        jid, rc = mvsmf.run(readback_jcl(a.unit, a.volume, dsn, rb, max(blksz, 3120)))
        row["readback_job"] = "%s %s" % (jid, rc)
        if not rc.endswith("0000"):
            raise RuntimeError("readback job %s ended %s" % (jid, rc))

        f = ftp_login(a)
        buf = io.BytesIO()
        f.voidcmd("TYPE I")
        f.retrbinary("RETR '%s'" % rb, buf.write)
        raw = buf.getvalue()
        got, anomalies, blocks = parse_blocks(raw)
        row["blocks"] = len(blocks)
        row["segments"] = sum(len(s) for _, s in blocks)
        row["anomalies"] = anomalies[:6]
        if mode == "text":
            row["complete"], row["detail"] = compare(expected, got)
        else:
            row["complete"], row["detail"] = compare_stream(payload, got, lrecl)
        if anomalies:
            row["complete"] = False
        row["layout"] = " | ".join(
            "%d:%s" % (b, ",".join("%d/%d" % s for s in segs[:6]) + ("..." if len(segs) > 6 else ""))
            for b, segs in blocks[:4])
        if not a.keep:
            delete(f, dsn)
            delete(f, rb)
    finally:
        try:
            f.close()
        except Exception:
            pass
    return row


LIMIT_DATA = LRECL - 8   # RDW and SDW both come out of LRECL (ftpd#432)
BIG_BLKSIZE_VB = 6233    # >= LRECL+4: VB 600 stays unspanned, limit LRECL-4


def readback(a, mvsmf, dsn, rb, blksz):
    f = ftp_login(a)
    try:
        delete(f, rb)
        jid, rc = mvsmf.run(readback_jcl(a.unit, a.volume, dsn, rb, max(blksz, 3120)))
        if not rc.endswith("0000"):
            raise RuntimeError("readback job %s ended %s" % (jid, rc))
        buf = io.BytesIO()
        f.voidcmd("TYPE I")
        f.retrbinary("RETR '%s'" % rb, buf.write)
        recs, anomalies, _ = parse_blocks(buf.getvalue())
        if not a.keep:
            delete(f, rb)
        return recs, anomalies
    finally:
        f.close()


def run_limit(a, mvsmf, hlq):
    """The longest text line of VBS 600/259.  Rows like run_case's."""
    dsn = "%s.TEST.SPLIMIT" % hlq
    rb = "%s.TEST.SPRBLIM" % hlq
    rows = []

    def line(n):
        return text_lines([n])[0].encode("ascii")

    def row(name, ok, code, detail, **kw):
        r = dict(target="LIMIT %d/%d" % (LRECL, BLKSIZE), mode=name, dsn=dsn,
                 stor_code=code, complete=ok, detail=detail, anomalies=[],
                 attrs_after="-")
        r.update(kw)
        rows.append(r)
        return r

    f = ftp_login(a)
    try:
        delete(f, dsn)
        jid, rc = mvsmf.run(alloc_jcl(a.unit, a.volume, dsn, LRECL, BLKSIZE))
        if not rc.endswith("0000"):
            raise RuntimeError("allocation job %s ended %s" % (jid, rc))

        for n in (LIMIT_DATA + 1, LIMIT_DATA + 4):
            code, reply = stor(f, "text", dsn, line(n) + b"\n")
            want = "(%d data bytes)" % LIMIT_DATA
            ok = code == "451" and want in reply
            row("%d bytes refused" % n, ok, code,
                "reply: %s" % reply if not ok else "451 names the limit")

        code, reply = stor(f, "text", dsn, line(LIMIT_DATA) + b"\n")
        recs, anomalies = readback(a, mvsmf, dsn, rb, BLKSIZE)
        ok = code.startswith("2") and recs == [line(LIMIT_DATA).decode().encode("cp037")] \
            and not anomalies
        row("%d bytes stored" % LIMIT_DATA, ok, code,
            "one record of %d bytes" % LIMIT_DATA if ok else
            "reply %s; records %s; %s" % (reply, [len(x) for x in recs], anomalies[:2]),
            anomalies=anomalies[:2])

        r = f.sendcmd("SITE TRUNCATE")
        if not r.startswith("200"):
            raise RuntimeError("SITE TRUNCATE: %s" % r)
        n = LIMIT_DATA + 4
        code, reply = stor(f, "text", dsn, line(n) + b"\n")
        recs, anomalies = readback(a, mvsmf, dsn, rb, BLKSIZE)
        ok = code.startswith("2") and "truncated" in reply \
            and recs == [line(n)[:LIMIT_DATA].decode().encode("cp037")] and not anomalies
        row("%d bytes truncated" % n, ok, code,
            "cut to %d bytes" % LIMIT_DATA if ok else
            "reply %s; records %s; %s" % (reply, [len(x) for x in recs], anomalies[:2]),
            anomalies=anomalies[:2])
        if not a.keep:
            delete(f, dsn)
    finally:
        try:
            f.close()
        except Exception:
            pass
    return rows


def run_limit_vb(a, mvsmf, hlq):
    """The longest text line of a VB data set FTPD allocates itself: 600/259 is
    made VBS by OPEN (limit 592), 600/6233 stays VB (limit 596)."""
    dsn = "%s.TEST.SPLIMVB" % hlq
    rb = "%s.TEST.SPRBLVB" % hlq
    rows = []

    def line(n):
        return text_lines([n])[0].encode("ascii")

    def row(target, name, ok, code, detail, **kw):
        r = dict(target=target, mode=name, dsn=dsn, stor_code=code,
                 complete=ok, detail=detail, anomalies=[], attrs_after="-")
        r.update(kw)
        rows.append(r)

    def site(f, blksz, truncate=False):
        cmds = ["SITE RECFM=VB", "SITE LRECL=%d" % LRECL, "SITE BLKSIZE=%d" % blksz,
                "SITE TRACKS", "SITE PRIMARY=10", "SITE SECONDARY=10"]
        for c in cmds + (["SITE TRUNCATE"] if truncate else []):
            r = f.sendcmd(c)
            if not r.startswith("200"):
                raise RuntimeError("%s: %s" % (c, r))

    def refused(f, tgt, n, limit):
        code, reply = stor(f, "text", dsn, line(n) + b"\n")
        ok = code == "451" and "(%d data bytes)" % limit in reply
        row(tgt, "%d bytes refused" % n, ok, code,
            "451 names the limit" if ok else "reply: %s" % reply)

    tgt = "LIMITVB %d/%d" % (LRECL, BLKSIZE)
    f = ftp_login(a)
    try:
        delete(f, dsn)
        site(f, BLKSIZE)
        for n in (LIMIT_DATA + 1, LIMIT_DATA + 4):
            refused(f, tgt, n, LIMIT_DATA)
        code, reply = stor(f, "text", dsn, line(LIMIT_DATA) + b"\n")
        attrs = list_attrs(f, dsn)
        ok = code.startswith("2") and attrs == "VBS %d %d" % (LRECL, BLKSIZE)
        row(tgt, "%d bytes stored, OPEN made it VBS" % LIMIT_DATA, ok, code,
            "data set is %s" % attrs if ok else "reply %s; data set %s" % (reply, attrs),
            attrs_after=attrs)
        delete(f, dsn)
        f.close()

        f = ftp_login(a)
        site(f, BLKSIZE, truncate=True)
        n = LIMIT_DATA + 4
        code, reply = stor(f, "text", dsn, line(n) + b"\n")
        attrs = list_attrs(f, dsn)
        ok = code.startswith("2") and "truncated" in reply \
            and attrs == "VBS %d %d" % (LRECL, BLKSIZE)
        row(tgt, "%d bytes truncated" % n, ok, code,
            "data set is %s" % attrs if ok else "reply %s; data set %s" % (reply, attrs),
            attrs_after=attrs)
        delete(f, dsn)
        f.close()

        tgt = "LIMITVB %d/%d" % (LRECL, BIG_BLKSIZE_VB)
        f = ftp_login(a)
        site(f, BIG_BLKSIZE_VB)
        code, reply = stor(f, "text", dsn, line(LRECL - 4) + b"\n")
        attrs = list_attrs(f, dsn)
        if code.startswith("2"):
            recs, anomalies = readback(a, mvsmf, dsn, rb, BIG_BLKSIZE_VB)
        else:
            recs, anomalies = [], []
        ok = code.startswith("2") and attrs == "VB %d %d" % (LRECL, BIG_BLKSIZE_VB) \
            and recs == [line(LRECL - 4).decode().encode("cp037")] and not anomalies
        row(tgt, "%d bytes stored, not spanned" % (LRECL - 4), ok, code,
            "one record of %d bytes" % (LRECL - 4) if ok else
            "reply %s; data set %s; records %s; %s" % (
                reply, attrs, [len(x) for x in recs], anomalies[:2]),
            attrs_after=attrs, anomalies=anomalies[:2])
        delete(f, dsn)
        refused(f, tgt, LRECL - 3, LRECL - 4)
    finally:
        try:
            f.close()
        except Exception:
            pass
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=2121)
    ap.add_argument("--user", default="IBMUSER")
    ap.add_argument("--pass", dest="password", default="SYS1")
    ap.add_argument("--mvsmf", default=os.environ.get("MBT_TARGET_MVSMF_URL"))
    ap.add_argument("--mvsmf-user", default=os.environ.get("MBT_TARGET_MVSMF_USER", "IBMUSER"))
    ap.add_argument("--mvsmf-password", default=os.environ.get("MBT_TARGET_MVSMF_PASSWORD", "SYS1"))
    ap.add_argument("--unit", default="SYSDA")
    ap.add_argument("--volume", default="")
    ap.add_argument("--keep", action="store_true", help="keep the data sets")
    ap.add_argument("--json", help="write the rows to FILE")
    ap.add_argument("--only", action="append", help="VBS, VB, BIG, LIMIT or LIMITVB")
    a = ap.parse_args()
    if not a.mvsmf:
        print("no mvsMF URL (--mvsmf or MBT_TARGET_MVSMF_URL)", file=sys.stderr)
        return 2
    mvsmf = Mvsmf(a.mvsmf, a.mvsmf_user, a.mvsmf_password)
    hlq = a.user.upper()

    rows, failed = [], False
    if not a.only or "LIMIT" in a.only:
        try:
            for row in run_limit(a, mvsmf, hlq):
                rows.append(row)
                print(json.dumps(row), flush=True)
        except Exception as e:
            rows.append(dict(target="LIMIT %d/%d" % (LRECL, BLKSIZE), mode="limit",
                             error="%s: %s" % (type(e).__name__, e), complete=None))
            failed = True
    if not a.only or "LIMITVB" in a.only:
        try:
            for row in run_limit_vb(a, mvsmf, hlq):
                rows.append(row)
                print(json.dumps(row), flush=True)
        except Exception as e:
            rows.append(dict(target="LIMITVB %d/%d" % (LRECL, BLKSIZE), mode="limit",
                             error="%s: %s" % (type(e).__name__, e), complete=None))
            failed = True
    for tag, kind, lrecl, blksz in CASES:
        if a.only and tag not in a.only:
            continue
        for mode in ("text", "binary"):
            try:
                row = run_case(a, mvsmf, hlq, tag, kind, lrecl, blksz, mode)
            except Exception as e:  # the case could not run: say so, go on
                row = dict(target="%s %d/%d" % (kind, lrecl, blksz), mode=mode,
                           error="%s: %s" % (type(e).__name__, e), complete=None)
                failed = True
            rows.append(row)
            print(json.dumps(row), flush=True)

    print()
    print("| Target | Mode | STOR reply | Data set after | Data complete | Jobs | Detail |")
    print("|---|---|---|---|---|---|---|")
    lost = False
    for r in rows:
        if r.get("complete") is None:
            print("| %s | %s | - | - | NOT RUN | - | %s |" % (r["target"], r["mode"], r["error"]))
            continue
        if not r["complete"]:
            lost = True
        jobs = ", ".join(r[k].split()[0] for k in ("alloc_job", "readback_job") if k in r)
        silent = " (SILENT: reply %s)" % r["stor_code"] \
            if not r["complete"] and r["stor_code"].startswith("2") else ""
        print("| %s | %s | %s | %s | %s | %s | %s |" % (
            r["target"], r["mode"], r["stor_code"], r["attrs_after"],
            ("yes" if r["complete"] else "NO") + silent, jobs or "-", r["detail"]
            + ("; " + "; ".join(r["anomalies"][:2]) if r["anomalies"] else "")))
    if a.json:
        with open(a.json, "w") as fh:
            json.dump(rows, fh, indent=1)
    return 2 if failed else (1 if lost else 0)


if __name__ == "__main__":
    sys.exit(main())
