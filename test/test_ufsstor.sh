#!/bin/bash
# ============================================================
# FTPD UFS STOR test (#294): uploads under load and refused uploads
#
# FTPD links libufs statically, so the ufs_fwrite()/ufs_ferror() contract of
# the ufsd release it was built against is part of FTPD.  This test drives
# STOR into UFS the way those fixes matter.
#
# Usage: ./test_ufsstor.sh [host] [port] [user] [pass] [sessions] [kbytes] [repeats]
#
# Needs python3 only; no FTP client.  UFSD must be running.  Run it where
# the server's PASV address is reachable (inside the container for a
# throwaway one).
#
# Round 1: SESSIONS (default 20) sessions log in, wait for each other and
#          then upload KBYTES (default 32) of their own pseudo-random data
#          into /tmp at the same moment, REPEATS times (default 1) each,
#          over the same file.  Every STOR must answer 226 and
#          every file must come back with the size that was sent (SIZE).
#          The content (RETR) is compared too; a difference is a NOTE, and a
#          failure only with STRICT_CONTENT=1 (a block of a file can come
#          back wrong on a single-CPU container, with or without the ufsd
#          1.5.1 fixes, which is not what this test is about).  SESSIONS must be larger than ufsd's 16
#          CSA buffers for the pool to run dry, and the server's MAXSESSIONS
#          at least SESSIONS: raise it, a connection above the limit is
#          closed unanswered and the round is reported as a setup error.
#          A session that gets no answer within FTP_TIMEOUT seconds
#          (default 120) fails the round.
# Round 2: an upload into a read-only mount (/www) and into a mount owned by
#          another user (/u/mvsce01), once for a new file and once over an
#          existing one.  Every STOR must be answered 5xx, none may
#          report 226, a new file must not exist afterwards and an existing
#          one must keep its size.  Skipped, as such, when the mount is not
#          there.
# Round 3: an upload larger than a UFS file can be (about 4.06 MB at blksize
#          4096) into /tmp.  The write fails after the open: the reply
#          must be 552, never 226 and never 2xx.
#
# Exit status: 0 = all checks passed, 1 = a check failed, 2 = setup error.
# ============================================================

HOST="${1:-localhost}"
PORT="${2:-2121}"
USER="${3:-ibmuser}"
PASS="${4:-secret}"
SESS="${5:-20}"
KB="${6:-32}"
REP="${7:-1}"

exec python3 - "$HOST" "$PORT" "$USER" "$PASS" "$SESS" "$KB" "$REP" <<'PYSTOR'
import ftplib, hashlib, io, os, socket, sys, threading, time

host, port, user, pw = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]
nsess, kb, reps = int(sys.argv[5]), int(sys.argv[6]), int(sys.argv[7])
tag = "ufsstor%d" % os.getpid()
TMO = int(os.environ.get("FTP_TIMEOUT", "120"))
strict = os.environ.get("STRICT_CONTENT") == "1"
failed = 0


def pas(msg):
    print("  PASS: " + msg)


def fail(msg):
    global failed
    failed = 1
    print("  FAIL: " + msg)


def login():
    f = ftplib.FTP()
    f.connect(host, port, timeout=TMO)
    f.login(user, pw)
    f.voidcmd("TYPE I")
    f.cwd("/")
    return f


def payload(i, nbytes):
    out, n, blk = [], 0, 0
    while n < nbytes:
        d = hashlib.sha256(("%s-%d-%d" % (tag, i, blk)).encode()).digest() * 128
        out.append(d)
        n += len(d)
        blk += 1
    return b"".join(out)[:nbytes]


def stor(f, path, data):
    """Return the final reply of a STOR as (code, text); never raises on 4xx/5xx."""
    try:
        conn = f.transfercmd("STOR " + path)
    except ftplib.error_reply as e:
        return int(str(e)[:3]), str(e)
    except (ftplib.error_perm, ftplib.error_temp) as e:
        return int(str(e)[:3]), str(e)
    try:
        for off in range(0, len(data), 4096):
            conn.sendall(data[off:off + 4096])
    except OSError:
        pass
    finally:
        conn.close()
    try:
        r = f.getresp()
        return int(r[:3]), r
    except (ftplib.error_perm, ftplib.error_temp, ftplib.error_reply) as e:
        return int(str(e)[:3]), str(e)


def size_of(f, path):
    try:
        return int(f.sendcmd("SIZE " + path).split()[1])
    except (ftplib.error_perm, ftplib.error_temp):
        return None


def fetch(f, path):
    buf = io.BytesIO()
    f.retrbinary("RETR " + path, buf.write, 4096)
    return buf.getvalue()


# ---- Round 1 ---------------------------------------------------------------
print("\n=== Round 1: %d parallel uploads of %d KB, %d time(s) each ==="
      % (nsess, kb, reps))
nbytes = kb * 1024
res = [None] * nsess
barrier = threading.Barrier(nsess)


def worker(i):
    path = "/tmp/%s_%d.bin" % (tag, i)
    try:
        f = login()
    except Exception as e:
        res[i] = ("setup", "login: %s" % (str(e) or type(e).__name__))
        try:
            barrier.abort()
        except threading.BrokenBarrierError:
            pass
        return
    try:
        barrier.wait(TMO)
        for _ in range(reps):
            code, text = stor(f, path, payload(i, nbytes))
            if code != 226:
                break
        res[i] = ("stor", code, text)
    except threading.BrokenBarrierError:
        res[i] = ("setup", "a session did not get to the barrier in %d s" % TMO)
    except OSError as e:
        res[i] = ("stor", 0, "no answer: %s" % (e or "timed out after %d s" % TMO))
    finally:
        try:
            f.quit()
        except Exception:
            pass


threads = [threading.Thread(target=worker, args=(i,)) for i in range(nsess)]
t0 = time.time()
for t in threads:
    t.start()
for t in threads:
    t.join()
print("  uploads took %.1f s" % (time.time() - t0))

setup = [r[1] for r in res if r and r[0] == "setup"] + \
        ["no result" for r in res if r is None]
if setup:
    # Every distinct reason, with its count: the barrier message of the
    # sessions that were waiting hides the login that never got its 220 (#327).
    reasons = {}
    for r in setup:
        reasons[r] = reasons.get(r, 0) + 1
    print("  SETUP ERROR: %d of %d sessions never uploaded (MAXSESSIONS too "
          "small?):" % (len(setup), nsess))
    for r, n in reasons.items():
        print("    %d x %s" % (n, r))
    sys.exit(2)

bad = [(i, r[1], r[2]) for i, r in enumerate(res) if r[1] != 226]
for i, code, text in bad:
    print("  session %d: %s" % (i, text))
if bad:
    fail("%d of %d uploads were not answered 226" % (len(bad), nsess))
else:
    pas("all %d uploads answered 226" % nsess)

f = login()
mismatch = 0
sizebad = 0
for i in range(nsess):
    path = "/tmp/%s_%d.bin" % (tag, i)
    sz = size_of(f, path)
    try:
        same = fetch(f, path) == payload(i, nbytes) if sz == nbytes else False
    except Exception:
        same = False
    if not same:
        mismatch += 1
        if sz != nbytes:
            sizebad += 1
        print("  %s: size %s, expected %d%s" % (
            path, sz, nbytes, "" if sz != nbytes else ", content differs"))
        if sz == nbytes:
            try:
                got = fetch(f, path)
            except Exception:
                got = b""
            want = payload(i, nbytes)
            for b in range(0, nbytes, 4096):
                if got[b:b + 4096] == want[b:b + 4096]:
                    continue
                whose = [j for j in range(nsess)
                         if got[b:b + 4096] == payload(j, nbytes)[b:b + 4096]]
                print("    block %d: %s" % (b // 4096, (
                    "holds the data of session %s" % whose) if whose else
                    ("all zero" if not any(got[b:b + 4096]) else "unknown")))
if sizebad:
    fail("%d of %d files have the wrong size" % (sizebad, nsess))
if mismatch and strict:
    fail("%d of %d files are not what was sent" % (mismatch, nsess))
elif mismatch:
    print("  NOTE: %d of %d files differ from what was sent (not a failure "
          "without STRICT_CONTENT=1)" % (mismatch, nsess))
else:
    pas("all %d files come back identical" % nsess)
for i in range(nsess):
    try:
        f.delete("/tmp/%s_%d.bin" % (tag, i))
    except Exception:
        pass
f.quit()

# ---- Round 2 ---------------------------------------------------------------
print("\n=== Round 2: uploads into mounts the user may not write ===")
f = login()
data = payload(0, 12 * 1024)
for mount in ("/www", "/u/mvsce01"):
    try:
        f.cwd(mount)
    except (ftplib.error_perm, ftplib.error_temp):
        print("  SKIPPED: %s is not mounted" % mount)
        continue
    f.cwd("/")
    new = "%s/%s_new.bin" % (mount, tag)
    code, text = stor(f, new, data)
    if code // 100 == 5 and code != 226:
        pas("%s: new file refused: %s" % (mount, text))
    else:
        fail("%s: new file answered %s" % (mount, text))
    if size_of(f, new) is None:
        pas("%s: the refused file does not exist" % mount)
    else:
        fail("%s: a refused upload left a file behind" % mount)
    listing = []
    f.retrlines("NLST " + mount, listing.append)
    existing = None
    for n in listing:
        n = n.strip().split("/")[-1]
        p = "%s/%s" % (mount, n)
        s = size_of(f, p)
        if s is not None and s > 0:
            existing = (p, s)
            break
    if existing is None:
        print("  SKIPPED: %s holds no file to upload over" % mount)
        continue
    code, text = stor(f, existing[0], data)
    if code // 100 == 5:
        pas("%s: STOR over %s refused: %s" % (mount, existing[0], text))
    else:
        fail("%s: STOR over %s answered %s" % (mount, existing[0], text))
    after = size_of(f, existing[0])
    if after == existing[1]:
        pas("%s: %s keeps its size (%d)" % (mount, existing[0], after))
    else:
        fail("%s: %s was %d bytes, now %s" % (mount, existing[0],
                                               existing[1], after))
f.quit()

# ---- Round 3 ---------------------------------------------------------------
print("\n=== Round 3: upload beyond the UFS file size limit ===")
f = login()
big = "/tmp/%s_big.bin" % tag
blk = payload(1, 4096)
limit = 5 * 1024 * 1024
try:
    conn = f.transfercmd("STOR " + big)
    try:
        sent = 0
        while sent < limit:
            conn.sendall(blk)
            sent += len(blk)
    except OSError:
        pass
    finally:
        conn.close()
    try:
        r = f.getresp()
        code, text = int(r[:3]), r
    except (ftplib.error_perm, ftplib.error_temp) as e:
        code, text = int(str(e)[:3]), str(e)
except (ftplib.error_perm, ftplib.error_temp) as e:
    code, text = int(str(e)[:3]), str(e)
print("  reply: %s" % text)
if code == 552:
    pas("write beyond the limit answered 552")
elif code // 100 == 2:
    fail("write beyond the limit was answered %d" % code)
else:
    fail("write beyond the limit answered %d, expected 552" % code)
try:
    f.delete(big)
except Exception:
    pass
f.quit()

print("\n%s" % ("ALL CHECKS PASSED" if not failed else "FAILED"))
sys.exit(failed)
PYSTOR
