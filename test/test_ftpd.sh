#!/bin/bash
# ============================================================
# FTPD Comprehensive Test Suite
# Uses tnftp (BSD ftp) for scripted FTP operations
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

HLQ=$(echo "$USER" | tr '[:lower:]' '[:upper:]')
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
# Helper: run FTP commands via tnftp script
# Captures server output for analysis.
# Usage: ftp_run "command1\ncommand2\n..." [capture_file]
# ============================================================
ftp_run() {
    local cmds="$1"
    local capture="${2:-/dev/null}"

    printf "user %s %s\n%b\nquit\n" "$USER" "$PASS" "$cmds" \
        | ftp -n -v -p -P "$PORT" "$HOST" > "$capture" 2>&1
    return $?
}

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

    local orig_md5=$(md5sum "$orig" 2>/dev/null | cut -d' ' -f1 || md5 -q "$orig")
    local back_md5=$(md5sum "$back" 2>/dev/null | cut -d' ' -f1 || md5 -q "$back")

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
FTP_OUT="$TMPDIR/ftp_bin_up.log"
ftp_run "$(cat <<CMDS
site recfm=fb
site lrecl=80
site blksize=3120
site primary=100
site secondary=50
site tracks
type binary
put $BINFILE '$DSN_BIN'
CMDS
)" "$FTP_OUT"
RC=$?
if [ $RC -eq 0 ] && ! grep -qi "^550\|^553\|^451" "$FTP_OUT"; then
    pass "Binary upload"
else
    fail "Binary upload (rc=$RC)"
    tail -5 "$FTP_OUT" | sed 's/^/    /'
fi

info "RETR -> $BINBACK"
FTP_OUT="$TMPDIR/ftp_bin_dn.log"
ftp_run "$(cat <<CMDS
type binary
get '$DSN_BIN' $BINBACK
CMDS
)" "$FTP_OUT"
RC=$?
if [ $RC -eq 0 ] && [ -f "$BINBACK" ]; then
    pass "Binary download"
else
    fail "Binary download (rc=$RC)"
    tail -5 "$FTP_OUT" | sed 's/^/    /'
fi

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
FTP_OUT="$TMPDIR/ftp_txt_up.log"
ftp_run "$(cat <<CMDS
site recfm=fb
site lrecl=80
site blksize=3120
site primary=10
site secondary=5
site tracks
type ascii
put $TXTFILE '$DSN_TXT'
CMDS
)" "$FTP_OUT"
RC=$?
if [ $RC -eq 0 ] && ! grep -qi "^550\|^553\|^451" "$FTP_OUT"; then
    pass "Text upload"
else
    fail "Text upload (rc=$RC)"
    tail -5 "$FTP_OUT" | sed 's/^/    /'
fi

info "RETR (TYPE A) -> $TXTBACK"
FTP_OUT="$TMPDIR/ftp_txt_dn.log"
ftp_run "$(cat <<CMDS
type ascii
get '$DSN_TXT' $TXTBACK
CMDS
)" "$FTP_OUT"
RC=$?
if [ $RC -eq 0 ] && [ -f "$TXTBACK" ]; then
    pass "Text download"
else
    fail "Text download (rc=$RC)"
    tail -5 "$FTP_OUT" | sed 's/^/    /'
fi

# For text, compare content after normalizing whitespace
if [ -f "$TXTBACK" ]; then
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

generate_binary_testfile "$MEMBIN" 8000   # 100 records x 80
generate_text_testfile "$MEMTXT"

# Create PDS (delete first if exists)
info "MKD '$DSN_PDS'"
ftp_run "$(cat <<CMDS
site recfm=fb
site lrecl=80
site blksize=3120
site primary=10
site secondary=5
site tracks
rmdir '$DSN_PDS'
mkdir '$DSN_PDS'
CMDS
)" /dev/null

# Upload binary member
info "STOR binary member -> '$DSN_PDS(BINMEM)'"
FTP_OUT="$TMPDIR/ftp_pds_binup.log"
ftp_run "$(cat <<CMDS
type binary
put $MEMBIN '$DSN_PDS(BINMEM)'
CMDS
)" "$FTP_OUT"
RC=$?
if [ $RC -eq 0 ] && ! grep -qi "^550\|^553\|^451" "$FTP_OUT"; then
    pass "PDS binary member upload"
else
    fail "PDS binary member upload (rc=$RC)"
    tail -5 "$FTP_OUT" | sed 's/^/    /'
fi

# Upload text member
info "STOR text member -> '$DSN_PDS(TXTMEM)'"
FTP_OUT="$TMPDIR/ftp_pds_txtup.log"
ftp_run "$(cat <<CMDS
type ascii
put $MEMTXT '$DSN_PDS(TXTMEM)'
CMDS
)" "$FTP_OUT"
RC=$?
if [ $RC -eq 0 ] && ! grep -qi "^550\|^553\|^451" "$FTP_OUT"; then
    pass "PDS text member upload"
else
    fail "PDS text member upload (rc=$RC)"
    tail -5 "$FTP_OUT" | sed 's/^/    /'
fi

# Download binary member
info "RETR binary member -> $MEMBIN_BACK"
FTP_OUT="$TMPDIR/ftp_pds_bindn.log"
ftp_run "$(cat <<CMDS
type binary
get '$DSN_PDS(BINMEM)' $MEMBIN_BACK
CMDS
)" "$FTP_OUT"
RC=$?
if [ $RC -eq 0 ] && [ -f "$MEMBIN_BACK" ]; then
    pass "PDS binary member download"
    compare_files "$MEMBIN" "$MEMBIN_BACK" "PDS binary member roundtrip"
else
    fail "PDS binary member download (rc=$RC)"
    tail -5 "$FTP_OUT" | sed 's/^/    /'
fi

# Download text member
info "RETR text member -> $MEMTXT_BACK"
FTP_OUT="$TMPDIR/ftp_pds_txtdn.log"
ftp_run "$(cat <<CMDS
type ascii
get '$DSN_PDS(TXTMEM)' $MEMTXT_BACK
CMDS
)" "$FTP_OUT"
RC=$?
if [ $RC -eq 0 ] && [ -f "$MEMTXT_BACK" ]; then
    pass "PDS text member download"
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
    tail -5 "$FTP_OUT" | sed 's/^/    /'
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
FTP_OUT="$TMPDIR/ftp_jes.log"
ftp_run "$(cat <<CMDS
site filetype=jes
type ascii
put $JCLFILE test_job.jcl
site filetype=seq
CMDS
)" "$FTP_OUT"
RC=$?

if [ $RC -eq 0 ]; then
    pass "JES job submission (rc=0)"
else
    fail "JES job submission (rc=$RC)"
    tail -5 "$FTP_OUT" | sed 's/^/    /'
fi

# Check if the server mentioned a JOB ID in the response
# Server sends: 250-It is known to JES as JOB00119
# tnftp verbose shows this as-is; extract JOBnnnnn or STCnnnnn or TSUnnnnn
if grep -o '[JST][OTU][BCS][0-9][0-9][0-9][0-9][0-9]' "$FTP_OUT" > /dev/null 2>&1; then
    JOBID=$(grep -o '[JST][OTU][BCS][0-9][0-9][0-9][0-9][0-9]' "$FTP_OUT" | head -1)
    pass "JES returned job ID: $JOBID"
else
    info "JES output:"
    cat "$FTP_OUT" | sed 's/^/    /'
    fail "JES did not return a job ID"
fi

# JES LIST — job listing
info "LIST in JES mode"
FTP_OUT="$TMPDIR/ftp_jes_list.log"
ftp_run "$(cat <<CMDS
site filetype=jes
ls
site filetype=seq
CMDS
)" "$FTP_OUT"
if grep -qi "JOBNAME" "$FTP_OUT"; then
    pass "JES LIST: header present"
else
    info "JES LIST output:"
    cat "$FTP_OUT" | sed 's/^/    /'
    fail "JES LIST: no job listing header"
fi

# JES LIST — spool files for submitted job
if [ -n "$JOBID" ]; then
    info "LIST $JOBID (spool files)"
    FTP_OUT="$TMPDIR/ftp_jes_spool.log"
    ftp_run "$(cat <<CMDS
site filetype=jes
ls $JOBID
site filetype=seq
CMDS
)" "$FTP_OUT"
    if grep -qi "DDNAME\|JESMSGLG\|JESJCL" "$FTP_OUT"; then
        pass "JES spool file listing"
    else
        info "JES spool output:"
        cat "$FTP_OUT" | sed 's/^/    /'
        fail "JES spool file listing: no spool files found"
    fi
fi

# JES RETR — spool retrieval (all spool files)
if [ -n "$JOBID" ]; then
    info "RETR $JOBID (all spool files)"
    SPOOL_ALL="$TMPDIR/spool_all.txt"
    FTP_OUT="$TMPDIR/ftp_jes_retr_all.log"
    ftp_run "$(cat <<CMDS
site filetype=jes
type ascii
get $JOBID $SPOOL_ALL
site filetype=seq
CMDS
)" "$FTP_OUT"
    if [ -f "$SPOOL_ALL" ] && [ -s "$SPOOL_ALL" ]; then
        pass "JES RETR all spool files ($(wc -c < "$SPOOL_ALL") bytes)"
    else
        fail "JES RETR all spool files: empty or missing"
        tail -5 "$FTP_OUT" | sed 's/^/    /'
    fi

    # JES RETR — specific spool file (file 1)
    info "RETR $JOBID.1 (first spool file)"
    SPOOL_ONE="$TMPDIR/spool_one.txt"
    FTP_OUT="$TMPDIR/ftp_jes_retr_one.log"
    ftp_run "$(cat <<CMDS
site filetype=jes
type ascii
get $JOBID.1 $SPOOL_ONE
site filetype=seq
CMDS
)" "$FTP_OUT"
    if [ -f "$SPOOL_ONE" ] && [ -s "$SPOOL_ONE" ]; then
        pass "JES RETR specific spool file ($(wc -c < "$SPOOL_ONE") bytes)"
    else
        fail "JES RETR specific spool file: empty or missing"
        tail -5 "$FTP_OUT" | sed 's/^/    /'
    fi

    # JES DELE — job purge
    info "DELE $JOBID (purge job)"
    FTP_OUT="$TMPDIR/ftp_jes_dele.log"
    ftp_run "$(cat <<CMDS
site filetype=jes
delete $JOBID
site filetype=seq
CMDS
)" "$FTP_OUT"
    if grep -qi "cancel\|250" "$FTP_OUT"; then
        pass "JES job purge"
    else
        info "JES DELE output:"
        cat "$FTP_OUT" | sed 's/^/    /'
        fail "JES job purge"
    fi
fi

# Verify FILETYPE=SEQ restores dataset mode
info "SITE FILETYPE=SEQ — verify dataset mode restored"
FTP_OUT="$TMPDIR/ftp_jes_seq.log"
ftp_run "$(cat <<CMDS
site recfm=fb
site lrecl=80
site blksize=3120
site primary=1
site secondary=1
site tracks
type ascii
put $JCLFILE '${HLQ}.TEST.JESBACK'
CMDS
)" "$FTP_OUT"
RC=$?
if [ $RC -eq 0 ] && ! grep -qi "^550\|^553\|^451" "$FTP_OUT"; then
    pass "STOR after FILETYPE=SEQ"
else
    fail "STOR after FILETYPE=SEQ (rc=$RC)"
    tail -5 "$FTP_OUT" | sed 's/^/    /'
fi

# ============================================================
# TEST 5: UFS Mode Switching (Phase 3a)
# ============================================================
section "Test 5: UFS Mode Switching"

# CWD / — should switch to UFS mode (250) or fail gracefully (550)
info "CWD / (UFS mode switch)"
FTP_OUT="$TMPDIR/ftp_ufs_cwd.log"
ftp_run "$(cat <<CMDS
cd /
pwd
CMDS
)" "$FTP_OUT"

if grep -qi "250.*HFS directory\|257.*/" "$FTP_OUT"; then
    pass "CWD / — UFS mode active"
    UFS_AVAILABLE=1

    # PWD should show UFS-style path
    if grep -qi '257.*"/"' "$FTP_OUT"; then
        pass "PWD in UFS mode shows /"
    else
        fail "PWD in UFS mode: expected /"
        grep -i "257" "$FTP_OUT" | sed 's/^/    /'
    fi

    # CWD back to MVS mode
    info "CWD '${HLQ}.' (back to MVS mode)"
    FTP_OUT="$TMPDIR/ftp_ufs_back.log"
    ftp_run "$(cat <<CMDS
cd /
cd '${HLQ}.'
pwd
CMDS
)" "$FTP_OUT"
    if grep -qi "257.*'${HLQ}\." "$FTP_OUT"; then
        pass "CWD back to MVS mode"
    else
        fail "CWD back to MVS mode"
        grep -i "257\|250\|550" "$FTP_OUT" | sed 's/^/    /'
    fi
elif grep -qi "550.*UFS service not available" "$FTP_OUT"; then
    pass "CWD / — UFSD not running, graceful 550"
    UFS_AVAILABLE=0
    info "UFSD not running — skipping further UFS tests"
else
    fail "CWD / — unexpected response"
    cat "$FTP_OUT" | sed 's/^/    /'
    UFS_AVAILABLE=0
fi

# ============================================================
# TEST 6: Cleanup (DELE)
# ============================================================
section "Test 6: Cleanup"

info "DELE '$DSN_BIN'"
FTP_OUT="$TMPDIR/ftp_cleanup.log"
ftp_run "delete '$DSN_BIN'" "$FTP_OUT"
! grep -qi "^550" "$FTP_OUT" && pass "Delete binary DS" || fail "Delete binary DS"

info "DELE '$DSN_TXT'"
ftp_run "delete '$DSN_TXT'" "$FTP_OUT"
! grep -qi "^550" "$FTP_OUT" && pass "Delete text DS" || fail "Delete text DS"

info "RMD '$DSN_PDS'"
ftp_run "rmdir '$DSN_PDS'" "$FTP_OUT"
! grep -qi "^550" "$FTP_OUT" && pass "Delete PDS" || fail "Delete PDS"

info "DELE '${HLQ}.TEST.JESBACK'"
ftp_run "delete '${HLQ}.TEST.JESBACK'" "$FTP_OUT"
! grep -qi "^550" "$FTP_OUT" && pass "Delete JES test DS" || fail "Delete JES test DS"

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
