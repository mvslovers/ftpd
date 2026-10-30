#!/bin/bash
# ============================================================
# FTPD burst test (#209): simultaneous connections
#
# The listener used to call accept() once per one-second timer tick, so N
# connections that arrived together were greeted one per second: the last of
# ten got its 220 about nine seconds after connecting.  It now drains the
# backlog on every tick, so all of them are answered on the same tick.
#
# Usage: ./test_burst.sh [host] [port] [maxsessions]
#
# Needs python3 only; no FTP client, no login.
#
# Round 1: MAXSESSIONS connections (default 10) are opened at the same
#          moment and the time of each 220 is taken from the start.  Pass:
#          all of them are greeted and the last 220 comes within 3 seconds.
#          The fixed server needs at most one tick (1 s) plus the session
#          start; the old one needed about one second per connection.
# Round 2: MAXSESSIONS + 2 connections at once.  Pass: exactly MAXSESSIONS
#          are greeted and the others are closed unanswered, as for
#          connections that arrive one by one -- the burst must not run past
#          the limit although the workers have not yet counted the sessions
#          the listener has queued.  A connection that is neither greeted
#          nor closed (queued behind a full pool) fails the round too.
#
# maxsessions must be the server's MAXSESSIONS (default 10, the FTPD default).
# Nothing else may be connected while it runs.
#
# The listen backlog (#327) shows only in a burst larger than 11: with a
# backlog of 10 the host held 11 completed connections and left the rest to
# SYN-ACK retransmits, so some of 30 got no 220 for seconds or at all.  Run it
# with MAXSESSIONS=40 on the server and `./test_burst.sh HOST PORT 40`: round 1
# then opens 40 at once and fails on such a server.  The default of 10 does
# not reach it.
#
# Exit status: 0 = all checks passed.
# ============================================================

HOST="${1:-localhost}"
PORT="${2:-2121}"
MAXS="${3:-10}"
LIMIT=3.0

FAILED=0
pass() { echo "  PASS: $1"; }
fail() { echo "  FAIL: $1"; FAILED=1; }

# burst N: prints "greeted=<g> of <n>", "hung=<h>" (neither greeted nor
# closed within 30 s), the sorted 220 times and "last=<s>"
burst() {
    python3 - "$HOST" "$PORT" "$1" <<'PYBURST'
import select, socket, sys, time

host, port, n = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
socks, t0 = {}, time.time()
for i in range(n):
    s = socket.socket()
    s.setblocking(False)
    s.connect_ex((host, port))
    socks[s] = i
buf, seen, deadline = {s: b"" for s in socks}, {}, t0 + 30
while len(seen) < n and time.time() < deadline:
    r, _, _ = select.select([s for s in socks if socks[s] not in seen], [], [], 0.2)
    for s in r:
        try:
            d = s.recv(512)
        except (BlockingIOError, ConnectionError):
            continue
        if not d:
            seen[socks[s]] = None
            continue
        buf[s] += d
        if b"\n" in buf[s]:
            seen[socks[s]] = (time.time() - t0) if buf[s].startswith(b"220") else None
for s in socks:
    try:
        s.send(b"QUIT\r\n")
    except OSError:
        pass
    s.close()
ok = sorted(t for t in seen.values() if t is not None)
print("greeted=%d of %d" % (len(ok), n))
print("hung=%d" % (n - len(seen)))
print("times=" + " ".join("%.2f" % t for t in ok))
print("last=%.2f" % (ok[-1] if ok else 99.0))
PYBURST
}

echo "FTPD burst test: $HOST:$PORT, MAXSESSIONS=$MAXS"
if [ "$MAXS" -lt 30 ]; then
    echo "  NOTE: a burst of $MAXS does not reach the listen backlog (#327);"
    echo "        run with MAXSESSIONS=40 and maxsessions 40 to cover it"
fi

echo "Round 1: $MAXS simultaneous connections"
OUT=$(burst "$MAXS")
echo "$OUT" | sed 's/^/    /'
GREETED=$(echo "$OUT" | sed -n 's/^greeted=\([0-9]*\) of.*/\1/p')
LAST=$(echo "$OUT" | sed -n 's/^last=//p')
if [ "$GREETED" != "$MAXS" ]; then
    fail "only ${GREETED:-0} of $MAXS connections got a 220"
elif awk -v t="$LAST" -v l="$LIMIT" 'BEGIN { exit !(t <= l) }'; then
    pass "$MAXS simultaneous connections all greeted within ${LAST}s"
else
    fail "last of $MAXS simultaneous connections greeted after ${LAST}s (limit ${LIMIT}s)"
fi

# the sessions of round 1 end on QUIT; give the workers a moment
sleep 5

EXTRA=$((MAXS + 2))
echo "Round 2: $EXTRA simultaneous connections, limit $MAXS"
OUT=$(burst "$EXTRA")
echo "$OUT" | sed 's/^/    /'
GREETED=$(echo "$OUT" | sed -n 's/^greeted=\([0-9]*\) of.*/\1/p')
HUNG=$(echo "$OUT" | sed -n 's/^hung=//p')
if [ "$GREETED" = "$MAXS" ] && [ "$HUNG" = "0" ]; then
    pass "exactly $MAXS of $EXTRA greeted, the rest refused"
else
    fail "$GREETED of $EXTRA greeted, $HUNG left hanging; expected exactly $MAXS greeted and the rest closed"
fi

if [ "$FAILED" -eq 0 ]; then
    echo "ALL CHECKS PASSED"
    exit 0
fi
echo "FAILED"
exit 1
