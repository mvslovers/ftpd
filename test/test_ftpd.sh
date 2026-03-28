#!/bin/bash
# ============================================================
# FTPD Comprehensive Test Suite
# Uses ncftpput/ncftpget for non-interactive operation
#
# Usage: ./test_ftpd.sh [host] [port] [user] [pass]
#
# Tests:
#   1. Binary roundtrip (FB 80 XMIT file)
#   2. Text (TYPE A) roundtrip with special characters
#   3. PDS creation (MKD) + member upload/download
#   4. JES job submission (SITE FILETYPE=JES)
#   5. Cleanup (DELE)
# ============================================================

HOST="${1:-localhost}"
PORT="${2:-2121}"
USER="${3:-ibmuser}"
PASS="${4:-secret}"

HLQ="IBMUSER"
TMPDIR="/tmp/ftpd_test_$$"
TOTAL=0
PASSED=0
FAILED=0
FAIL_LIST=""

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; BOLD='\033[1m'; NC='\033[0m'

pass() {
    echo -e "  ${GREEN}PASS${NC}: $1"
    PASSED=$((PASSED + 1))
    TOTAL=$((TOTAL + 1))
}
fail() {
    echo -e "  ${RED}FAIL${NC}: $1"
    FAILED=$((FAILED + 1))
    TOTAL=$((TOTAL + 1))
    FAIL_LIST="${FAIL_LIST}\n  - $1"
}
info() { echo -e "  ${YELLOW}INFO${NC}: $1"; }
section() { echo -e "\n${CYAN}${BOLD}=== $1 ===${NC}"; }

# --- Setup ---
mkdir -p "$TMPDIR"
cleanup() { rm -rf "$TMPDIR"; }
trap cleanup EXIT

echo -e "${BOLD}FTPD Comprehensive Test Suite${NC}"
echo "Target: $USER@$HOST:$PORT"
echo "Temp:   $TMPDIR"

# ============================================================
# Helper: generate binary test file (FB 80 XMIT-like)
# ============================================================
generate_binary_testfile() {
    local outfile="$1"
    local size="${2:-710400}"  # default ~693KB
    dd if=/dev/urandom of="$outfile" bs="$size" count=1 2>/dev/null
    # Ensure size is multiple of 80 (LRECL)
    local actual=$(stat -c%s "$outfile" 2>/dev/null || stat -f%z "$outfile")
    local aligned=$(( (actual / 80) * 80 ))
    if [ "$aligned" -ne "$actual" ]; then
        truncate -s "$aligned" "$outfile" 2>/dev/null || \
            dd if="$outfile" of="${outfile}.tmp" bs="$aligned" count=1 2>/dev/null && \
            mv "${outfile}.tmp" "$outfile"
    fi
}

# ============================================================
# Helper: generate text test file with special characters
# ============================================================
generate_text_testfile() {
    local outfile="$1"
    cat > "$outfile" << 'TEXTEOF'
* FTPD Test File — Special Characters
* ====================================
*
* Square brackets:    [HELLO] [WORLD]
* Curly braces:       {BEGIN} {END}
* Pipe:               ONE | TWO | THREE
* Backslash:          PATH\TO\FILE
* Dollar sign:        $HLQ.$DATASET
* At sign:            USER@HOST
* Hash:               #INCLUDE #DEFINE
* Tilde:              ~/home/user
* Exclamation:        STOP! GO! WAIT!
* Percent:            100% COMPLETE
* Caret:              2^10 = 1024
* Ampersand:          A & B & C
* Asterisk:           *.LOAD **.DATA
* Semicolon:          A=1; B=2; C=3
* Colon:              HH:MM:SS
* Single quotes:      CWD 'SYS1.MACLIB'
* Double quotes:      MSG="HELLO WORLD"
* Less/Greater:       IF A < B THEN C > D
* Question mark:      WHERE IS IT?
* Comma:              A, B, C, D
* Period:             SYS1.MACLIB.MEMBER
* Parentheses:        IEFBR14(MEMBER)
* Plus/Minus:         A + B - C
* Equals:             X = Y
* Underscore:         MY_VARIABLE
* Numeric:            0123456789
* German umlauts:     (not in EBCDIC CP037 base)
* Mixed case:         AbCdEfGhIjKlMnOpQrStUvWxYz
*
* JCL-like content:
//TESTJOB  JOB (ACCT),'TEST',CLASS=A,MSGCLASS=X
//STEP1    EXEC PGM=IEFBR14
//SYSPRINT DD SYSOUT=*
//SYSIN    DD DUMMY
//DD1      DD DSN=SYS1.MACLIB(ABEND),DISP=SHR
/*
*
* End of test file.
TEXTEOF
}

# ============================================================
# Helper: FTP upload
# ============================================================
ftp_put() {
    local localfile="$1"
    local remotename="$2"
    shift 2
    # remaining args are -W options
    ncftpput -u "$USER" -p "$PASS" -P "$PORT" "$@" \
        -C "$HOST" "$localfile" "$remotename" 2>&1
}

# Helper: FTP download
ftp_get() {
    local remotename="$1"
    local localfile="$2"
    shift 2
    ncftpget -u "$USER" -p "$PASS" -P "$PORT" "$@" \
        -C "$HOST" "$remotename" "$localfile" 2>&1
}

# Helper: FTP command (abuse ncftpput with /dev/null)
ftp_cmd() {
    # Send raw FTP commands via -W, upload /dev/null to dummy
    ncftpput -u "$USER" -p "$PASS" -P "$PORT" "$@" \
        -C "$HOST" /dev/null "'${HLQ}.DUMMY.NULL'" 2>&1 || true
}

# Helper: compare files
compare_files() {
    local orig="$1"
    local back="$2"
    local label="$3"

    local origsize=$(stat -c%s "$orig" 2>/dev/null || stat -f%z "$orig")
    local backsize=$(stat -c%s "$back" 2>/dev/null || stat -f%z "$back" || echo 0)

    if [ "$origsize" != "$backsize" ]; then
        fail "$label: Size mismatch (orig=$origsize back=$backsize)"
        return 1
    fi

    local orig_md5=$(md5sum "$orig" | cut -d' ' -f1)
    local back_md5=$(md5sum "$back" | cut -d' ' -f1)

    if [ "$orig_md5" = "$back_md5" ]; then
        pass "$label: MD5 match ($orig_md5)"
        return 0
    else
        fail "$label: MD5 mismatch"
        echo "    Original:  $orig_md5"
        echo "    Roundtrip: $back_md5"
        local diffbyte=$(cmp -l "$orig" "$back" 2>/dev/null | head -1 | awk '{print $1}')
        if [ -n "$diffbyte" ]; then
            info "First diff at byte $diffbyte"
            local hexstart=$((diffbyte > 16 ? diffbyte - 16 : 0))
            echo "    Original:"
            xxd -s $hexstart -l 48 "$orig" | sed 's/^/    /'
            echo "    Roundtrip:"
            xxd -s $hexstart -l 48 "$back" | sed 's/^/    /'
        fi
        return 1
    fi
}

# ============================================================
# TEST 1: Binary Roundtrip (FB 80)
# ============================================================
section "Test 1: Binary Roundtrip (FB 80)"

BINFILE="$TMPDIR/test_binary.dat"
BINBACK="$TMPDIR/test_binary_back.dat"
DSN_BIN="${HLQ}.TEST.BINARY"

generate_binary_testfile "$BINFILE" 710400
BINSIZE=$(stat -c%s "$BINFILE" 2>/dev/null || stat -f%z "$BINFILE")
info "Generated $BINSIZE bytes ($((BINSIZE/80)) records)"

info "STOR -> '$DSN_BIN'"
ftp_put "$BINFILE" "'$DSN_BIN'" \
    -W "DELE $DSN_BIN" \
    -W "SITE RECFM=FB" \
    -W "SITE LRECL=80" \
    -W "SITE BLKSIZE=3120" \
    -W "SITE PRIMARY=100" \
    -W "SITE SECONDARY=50" \
    -W "SITE TRACKS" > /dev/null 2>&1
RC=$?
[ $RC -eq 0 ] && pass "Binary upload" || fail "Binary upload (rc=$RC)"

info "RETR -> $BINBACK"
ftp_get "'$DSN_BIN'" "$BINBACK" > /dev/null 2>&1
RC=$?
[ $RC -eq 0 ] && [ -f "$BINBACK" ] && pass "Binary download" || fail "Binary download (rc=$RC)"

compare_files "$BINFILE" "$BINBACK" "Binary roundtrip"

# ============================================================
# TEST 2: Text (TYPE A) Roundtrip (FB 80)
# ============================================================
section "Test 2: Text (TYPE A) Roundtrip — Special Characters"

TXTFILE="$TMPDIR/test_text.txt"
TXTBACK="$TMPDIR/test_text_back.txt"
DSN_TXT="${HLQ}.TEST.TEXT"

generate_text_testfile "$TXTFILE"
TXTSIZE=$(stat -c%s "$TXTFILE" 2>/dev/null || stat -f%z "$TXTFILE")
TXTLINES=$(wc -l < "$TXTFILE")
info "Generated $TXTSIZE bytes, $TXTLINES lines"

info "STOR (TYPE A) -> '$DSN_TXT'"
ftp_put "$TXTFILE" "'$DSN_TXT'" \
    -a \
    -W "DELE $DSN_TXT" \
    -W "SITE RECFM=FB" \
    -W "SITE LRECL=80" \
    -W "SITE BLKSIZE=3120" \
    -W "SITE PRIMARY=10" \
    -W "SITE SECONDARY=5" \
    -W "SITE TRACKS" > /dev/null 2>&1
RC=$?
[ $RC -eq 0 ] && pass "Text upload" || fail "Text upload (rc=$RC)"

info "RETR (TYPE A) -> $TXTBACK"
ftp_get "'$DSN_TXT'" "$TXTBACK" -a > /dev/null 2>&1
RC=$?
[ $RC -eq 0 ] && [ -f "$TXTBACK" ] && pass "Text download" || fail "Text download (rc=$RC)"

# For text, we can't do byte-for-byte compare (CRLF, trailing blanks, etc.)
# Instead, compare content after normalizing whitespace
if [ -f "$TXTBACK" ]; then
    # Strip trailing whitespace and normalize line endings
    sed 's/[[:space:]]*$//' "$TXTFILE"  | tr -d '\r' > "$TMPDIR/txt_orig_norm"
    sed 's/[[:space:]]*$//' "$TXTBACK"  | tr -d '\r' > "$TMPDIR/txt_back_norm"

    if diff -q "$TMPDIR/txt_orig_norm" "$TMPDIR/txt_back_norm" > /dev/null 2>&1; then
        pass "Text roundtrip: content matches"
    else
        fail "Text roundtrip: content differs"
        diff -u "$TMPDIR/txt_orig_norm" "$TMPDIR/txt_back_norm" | head -30 | sed 's/^/    /'
    fi

    # Check specific special characters survived
    for char in '[' ']' '{' '}' '|' '$' '@' '#'; do
        if grep -qF "$char" "$TXTBACK"; then
            pass "Special char '$char' survived roundtrip"
        else
            fail "Special char '$char' MISSING after roundtrip"
        fi
    done
fi

# ============================================================
# TEST 3: PDS Creation + Member Upload/Download
# ============================================================
section "Test 3: PDS Creation (MKD) + Member Operations"

DSN_PDS="${HLQ}.TEST.PDS"
MEMBIN="$TMPDIR/member_bin.dat"
MEMTXT="$TMPDIR/member_txt.txt"
MEMBIN_BACK="$TMPDIR/member_bin_back.dat"
MEMTXT_BACK="$TMPDIR/member_txt_back.txt"

# Generate small test files for members
generate_binary_testfile "$MEMBIN" 8000   # 100 records x 80
generate_text_testfile "$MEMTXT"

# Delete PDS if exists, then create via MKD
info "MKD '$DSN_PDS'"
ftp_cmd -W "RMD '$DSN_PDS'" > /dev/null 2>&1 || true
ftp_cmd \
    -W "SITE RECFM=FB" \
    -W "SITE LRECL=80" \
    -W "SITE BLKSIZE=3120" \
    -W "SITE PRIMARY=10" \
    -W "SITE SECONDARY=5" \
    -W "SITE TRACKS" \
    -W "MKD '$DSN_PDS'" > /dev/null 2>&1

# Upload binary member
info "STOR binary member -> '$DSN_PDS(BINMEM)'"
ftp_put "$MEMBIN" "'$DSN_PDS(BINMEM)'" > /dev/null 2>&1
RC=$?
[ $RC -eq 0 ] && pass "PDS binary member upload" || fail "PDS binary member upload (rc=$RC)"

# Upload text member
info "STOR text member -> '$DSN_PDS(TXTMEM)'"
ftp_put "$MEMTXT" "'$DSN_PDS(TXTMEM)'" -a > /dev/null 2>&1
RC=$?
[ $RC -eq 0 ] && pass "PDS text member upload" || fail "PDS text member upload (rc=$RC)"

# Download binary member
info "RETR binary member -> $MEMBIN_BACK"
ftp_get "'$DSN_PDS(BINMEM)'" "$MEMBIN_BACK" > /dev/null 2>&1
RC=$?
if [ $RC -eq 0 ] && [ -f "$MEMBIN_BACK" ]; then
    pass "PDS binary member download"
    compare_files "$MEMBIN" "$MEMBIN_BACK" "PDS binary member roundtrip"
else
    fail "PDS binary member download (rc=$RC)"
fi

# Download text member
info "RETR text member -> $MEMTXT_BACK"
ftp_get "'$DSN_PDS(TXTMEM)'" "$MEMTXT_BACK" -a > /dev/null 2>&1
RC=$?
if [ $RC -eq 0 ] && [ -f "$MEMTXT_BACK" ]; then
    pass "PDS text member download"
    # Normalize and compare
    sed 's/[[:space:]]*$//' "$MEMTXT"      | tr -d '\r' > "$TMPDIR/mem_orig_norm"
    sed 's/[[:space:]]*$//' "$MEMTXT_BACK" | tr -d '\r' > "$TMPDIR/mem_back_norm"
    if diff -q "$TMPDIR/mem_orig_norm" "$TMPDIR/mem_back_norm" > /dev/null 2>&1; then
        pass "PDS text member roundtrip: content matches"
    else
        fail "PDS text member roundtrip: content differs"
        diff -u "$TMPDIR/mem_orig_norm" "$TMPDIR/mem_back_norm" | head -20 | sed 's/^/    /'
    fi
else
    fail "PDS text member download (rc=$RC)"
fi

# ============================================================
# TEST 4: JES Job Submission (SITE FILETYPE=JES)
# ============================================================
section "Test 4: JES Job Submission"

JCLFILE="$TMPDIR/test_job.jcl"
cat > "$JCLFILE" << 'JCLEOF'
//FTPDTEST JOB (ACCT),'FTPD TEST',CLASS=A,MSGCLASS=X
//STEP1    EXEC PGM=IEFBR14
//SYSPRINT DD SYSOUT=*
JCLEOF

info "SITE FILETYPE=JES + STOR JCL"
JES_OUT=$(ncftpput -u "$USER" -p "$PASS" -P "$PORT" \
    -W "SITE FILETYPE=JES" \
    -C "$HOST" "$JCLFILE" "test_job.jcl" 2>&1)
RC=$?

if [ $RC -eq 0 ]; then
    pass "JES job submission (rc=0)"
else
    fail "JES job submission (rc=$RC)"
fi

# Check if the server mentioned a JOB ID in the response
if echo "$JES_OUT" | grep -qi "JOB[0-9]"; then
    JOBID=$(echo "$JES_OUT" | grep -oi "JOB[0-9]*" | head -1)
    pass "JES returned job ID: $JOBID"
else
    info "JES output: $JES_OUT"
    fail "JES did not return a job ID"
fi

# Switch back to SEQ mode and verify STOR still works
info "SITE FILETYPE=SEQ — verify dataset mode restored"
ftp_put "$JCLFILE" "'${HLQ}.TEST.JESBACK'" \
    -a \
    -W "SITE FILETYPE=SEQ" \
    -W "SITE RECFM=FB" \
    -W "SITE LRECL=80" \
    -W "SITE BLKSIZE=3120" \
    -W "SITE PRIMARY=1" \
    -W "SITE SECONDARY=1" \
    -W "SITE TRACKS" > /dev/null 2>&1
RC=$?
[ $RC -eq 0 ] && pass "STOR after FILETYPE=SEQ" || fail "STOR after FILETYPE=SEQ (rc=$RC)"

# ============================================================
# TEST 5: Cleanup (DELE)
# ============================================================
section "Test 5: Cleanup"

info "DELE '$DSN_BIN'"
ftp_cmd -W "DELE $DSN_BIN" > /dev/null 2>&1 && pass "Delete binary DS" || fail "Delete binary DS"

info "DELE '$DSN_TXT'"
ftp_cmd -W "DELE $DSN_TXT" > /dev/null 2>&1 && pass "Delete text DS" || fail "Delete text DS"

info "RMD '$DSN_PDS'"
ftp_cmd -W "RMD '$DSN_PDS'" > /dev/null 2>&1 && pass "Delete PDS" || fail "Delete PDS"

info "DELE '${HLQ}.TEST.JESBACK'"
ftp_cmd -W "DELE ${HLQ}.TEST.JESBACK" > /dev/null 2>&1 && pass "Delete JES test DS" || fail "Delete JES test DS"

# ============================================================
# Summary
# ============================================================
echo ""
echo -e "${BOLD}================================${NC}"
echo -e "${BOLD}  Results: $PASSED/$TOTAL passed${NC}"
if [ $FAILED -gt 0 ]; then
    echo -e "${RED}  $FAILED FAILED:${NC}"
    echo -e "$FAIL_LIST"
    echo -e "${BOLD}================================${NC}"
    echo ""
    info "Files kept in $TMPDIR for analysis"
    trap - EXIT  # don't cleanup on failure
    exit 1
else
    echo -e "${GREEN}  ALL TESTS PASSED${NC}"
    echo -e "${BOLD}================================${NC}"
    exit 0
fi
