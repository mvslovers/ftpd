#!/bin/bash
# ============================================================
# FTPD Comprehensive Test Suite
# Uses tnftp (BSD ftp) for scripted FTP operations
#
# Usage: ./test_ftpd.sh [host] [port] [user] [pass]
#
# Tests:
#   1.  Binary roundtrip (TYPE I, FB 80 XMIT file)
#   2.  Text (TYPE A) roundtrip with special characters
#   2b. MVS TYPE E roundtrip (EBCDIC transfer)
#   3.  PDS creation (MKD) + member upload/download
#   4.  JES job submission (SITE FILETYPE=JES)
#   5.  UFS mode switching (CWD / / CWD 'DSN')
#   5b. UFS file ops: TYPE A + TYPE I roundtrip, LIST, SIZE, DELE, MKD/RMD
#   6.  Cleanup (DELE/RMD)
#   7.  Out of space (x37): permanent 552, no hang, data set scratched
#   8.  Failed member STOR into a full PDS: 552, directory untouched
#   16. Text STOR/RETR in RECFM=VB, VBA and U: one record per line (#185, #245)
#   9.  APPE: appends to a sequential data set, creates what is missing,
#       refuses an existing PDS member (550, unchanged), 502 for UFS
#   10. SITE in any spelling: verb, keywords and values (quote form)
#   11. Member STOR into a PDS with a full directory: 552 + SITE DIRECTORY=;
#       the full PDS can then be renamed, its old name reused and deleted
#   12. STOR and APPE on a retained data set: 550 with the date, no IEC507D
#   13. Byte counters: F FTPD,STATS counts every transferred byte once
#       (needs FTPD_CONSOLE, see below)
#   14. FEAT, SIZE and MDTM in the MVS and in the UFS view: one FEAT reply
#       in both, 502 for SIZE/MDTM on data sets, 213 for UFS
#   15. Settings that took effect nowhere: SITE TRUNCATE on STOR, SITE RDW and
#       SBSENDEOL refused, SITE JESSTATUS filters the JES listing
#   17. SITE DCBDSN: RECFM, LRECL and BLKSIZE taken from a model data set
#   18. JES spool authorization (#90): who sees, reads and deletes which
#       job (22 cases, RACF class JESSPOOL; needs RAKF 2.0.0 and a RAKF
#       administrator as test user, see the comment of Test 18)
#
# Test 13 reads the operator command F FTPD,STATS, which an FTP client cannot
# reach.  Set FTPD_CONSOLE to a command that sends one operator command and
# prints the system's answer, for example
#     FTPD_CONSOLE='mbt target console env --' ./test_ftpd.sh ...
# Without it Test 13 is reported as SKIPPED, not as passed.
#
# Test 18 changes SYS1.SECURE.CNTL(PROFILES) and (USERS) of the system under
# test and puts them back.  Run it against a throwaway container only.
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
# TEST 2b: MVS TYPE E Roundtrip (EBCDIC transfer)
# ============================================================
section "Test 2b: MVS TYPE E Roundtrip"

EFILE="$TMPDIR/test_ebcdic.txt"
EBACK="$TMPDIR/test_ebcdic_back.txt"
DSN_EBC="${HLQ}.TEST.EBCDIC"

generate_text_testfile "$EFILE"

info "STOR (TYPE E) -> '$DSN_EBC'"
FTP_OUT="$TMPDIR/ftp_ebc_up.log"
ftp_run "$(cat <<CMDS
site recfm=fb
site lrecl=80
site blksize=3120
site primary=10
site secondary=5
site tracks
type ebcdic
put $EFILE '$DSN_EBC'
CMDS
)" "$FTP_OUT"
RC=$?
if [ $RC -eq 0 ] && ! grep -qi "^550\|^553\|^451\|^504" "$FTP_OUT"; then
    pass "MVS TYPE E upload"
else
    fail "MVS TYPE E upload (rc=$RC)"
    tail -5 "$FTP_OUT" | sed 's/^/    /'
fi

info "RETR (TYPE E) -> $EBACK"
FTP_OUT="$TMPDIR/ftp_ebc_dn.log"
ftp_run "$(cat <<CMDS
type ebcdic
get '$DSN_EBC' $EBACK
CMDS
)" "$FTP_OUT"
RC=$?
if [ $RC -eq 0 ] && grep -qi "250\|Transfer complete" "$FTP_OUT"; then
    pass "MVS TYPE E download"
    # Note: tnftp does NOT convert EBCDIC↔ASCII client-side.
    # The file may be empty (tnftp discards unrecognized encoding)
    # or contain raw EBCDIC bytes.  We verify the server-side transfer
    # succeeded (250 reply).  True TYPE E roundtrip validation requires
    # a client that speaks EBCDIC (e.g. z/OS FTP or a custom test).
    if [ -f "$EBACK" ] && [ -s "$EBACK" ]; then
        EBACKSIZE=$(stat -c%s "$EBACK" 2>/dev/null || stat -f%z "$EBACK")
        pass "MVS TYPE E roundtrip: $EBACKSIZE bytes received"
    else
        info "tnftp wrote 0 bytes (expected — no EBCDIC support in client)"
        pass "MVS TYPE E roundtrip: server transfer OK (250)"
    fi
else
    fail "MVS TYPE E download (rc=$RC)"
    tail -5 "$FTP_OUT" | sed 's/^/    /'
fi

# Cleanup TYPE E dataset
ftp_run "delete '$DSN_EBC'" /dev/null

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
    # tnftp shows "Remote directory: /" or the raw 257 response
    if grep -qi '257\|HFS.*working directory\|Remote directory.*/' "$FTP_OUT"; then
        pass "PWD in UFS mode shows /"
    else
        fail "PWD in UFS mode: expected /"
        cat "$FTP_OUT" | sed 's/^/    /'
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
    # Look for MVS-style CWD response (250) or PWD response (257)
    if grep -qi "250.*${HLQ}\.\|257.*${HLQ}\." "$FTP_OUT"; then
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
# TEST 5b: UFS File Operations (Phase 3b) — only if UFSD running
# ============================================================
if [ "${UFS_AVAILABLE:-0}" = "1" ]; then
section "Test 5b: UFS File Operations"

UFS_TESTDIR="/tmp/ftpdtest$$"

# MKD — create test directory
info "MKD $UFS_TESTDIR"
FTP_OUT="$TMPDIR/ftp_ufs_mkd.log"
ftp_run "$(cat <<CMDS
cd /
mkdir $UFS_TESTDIR
CMDS
)" "$FTP_OUT"
if grep -qi "257\|directory created" "$FTP_OUT"; then
    pass "MKD $UFS_TESTDIR"
else
    fail "MKD $UFS_TESTDIR"
    cat "$FTP_OUT" | sed 's/^/    /'
fi

# --- UFS Text (TYPE A) roundtrip with special characters ---
info "UFS TYPE A roundtrip — special characters"
UFS_TXTFILE="$TMPDIR/ufs_text.txt"
UFS_TXTBACK="$TMPDIR/ufs_text_back.txt"
generate_text_testfile "$UFS_TXTFILE"

FTP_OUT="$TMPDIR/ftp_ufs_txt_up.log"
ftp_run "$(cat <<CMDS
cd $UFS_TESTDIR
type ascii
put $UFS_TXTFILE specials.txt
CMDS
)" "$FTP_OUT"
if grep -qi "226\|Transfer complete" "$FTP_OUT"; then
    pass "UFS STOR (TYPE A) specials.txt"
else
    fail "UFS STOR (TYPE A) specials.txt"
    cat "$FTP_OUT" | sed 's/^/    /'
fi

FTP_OUT="$TMPDIR/ftp_ufs_txt_dn.log"
ftp_run "$(cat <<CMDS
cd $UFS_TESTDIR
type ascii
get specials.txt $UFS_TXTBACK
CMDS
)" "$FTP_OUT"
if [ -f "$UFS_TXTBACK" ]; then
    pass "UFS RETR (TYPE A) specials.txt"

    # Normalize trailing whitespace + CR for comparison
    sed 's/[[:space:]]*$//' "$UFS_TXTFILE" | tr -d '\r' > "$TMPDIR/ufs_txt_orig_norm"
    sed 's/[[:space:]]*$//' "$UFS_TXTBACK" | tr -d '\r' > "$TMPDIR/ufs_txt_back_norm"

    if diff -q "$TMPDIR/ufs_txt_orig_norm" "$TMPDIR/ufs_txt_back_norm" > /dev/null 2>&1; then
        pass "UFS TYPE A roundtrip: content matches"
    else
        fail "UFS TYPE A roundtrip: content differs"
        diff -u "$TMPDIR/ufs_txt_orig_norm" "$TMPDIR/ufs_txt_back_norm" | head -30 | sed 's/^/    /'
    fi

    # Check special characters survived IBM-1047 roundtrip
    for char in '[' ']' '{' '}' '|' '$' '@' '#' '~' '!' '^'; do
        if grep -qF "$char" "$UFS_TXTBACK"; then
            pass "UFS special char '$char' survived 1047 roundtrip"
        else
            fail "UFS special char '$char' MISSING after 1047 roundtrip"
        fi
    done
else
    fail "UFS RETR (TYPE A) specials.txt — file missing"
fi

# --- UFS Binary (TYPE I) roundtrip ---
info "UFS TYPE I roundtrip — binary"
UFS_BINFILE="$TMPDIR/ufs_binary.dat"
UFS_BINBACK="$TMPDIR/ufs_binary_back.dat"
generate_binary_testfile "$UFS_BINFILE" 8192

FTP_OUT="$TMPDIR/ftp_ufs_bin_up.log"
ftp_run "$(cat <<CMDS
cd $UFS_TESTDIR
type binary
put $UFS_BINFILE binary.dat
CMDS
)" "$FTP_OUT"
if grep -qi "226\|Transfer complete" "$FTP_OUT"; then
    pass "UFS STOR (TYPE I) binary.dat"
else
    fail "UFS STOR (TYPE I) binary.dat"
    cat "$FTP_OUT" | sed 's/^/    /'
fi

FTP_OUT="$TMPDIR/ftp_ufs_bin_dn.log"
ftp_run "$(cat <<CMDS
cd $UFS_TESTDIR
type binary
get binary.dat $UFS_BINBACK
CMDS
)" "$FTP_OUT"
if [ -f "$UFS_BINBACK" ]; then
    pass "UFS RETR (TYPE I) binary.dat"
    compare_files "$UFS_BINFILE" "$UFS_BINBACK" "UFS binary roundtrip"
else
    fail "UFS RETR (TYPE I) binary.dat — file missing"
fi

# --- UFS LIST format check ---
info "LIST $UFS_TESTDIR (format check)"
FTP_OUT="$TMPDIR/ftp_ufs_list.log"
ftp_run "$(cat <<CMDS
cd $UFS_TESTDIR
ls
CMDS
)" "$FTP_OUT"
if grep -qi "specials.txt" "$FTP_OUT" && grep -qi "binary.dat" "$FTP_OUT"; then
    pass "UFS LIST shows both files"
else
    fail "UFS LIST: expected files not found"
    cat "$FTP_OUT" | sed 's/^/    /'
fi

# Check Unix-style format (drwx... or -rw-...)
if grep -qE '^[d-][rwx-]{9}' "$FTP_OUT"; then
    pass "UFS LIST format is Unix-style"
else
    info "UFS LIST output (format check):"
    cat "$FTP_OUT" | sed 's/^/    /'
    fail "UFS LIST format: not Unix-style"
fi

# --- UFS SIZE ---
info "SIZE specials.txt"
FTP_OUT="$TMPDIR/ftp_ufs_size.log"
ftp_run "$(cat <<CMDS
cd $UFS_TESTDIR
size specials.txt
CMDS
)" "$FTP_OUT"
# tnftp shows "filename\tsize" or raw "213 size"
if grep -qi "213\|specials.txt" "$FTP_OUT" && grep -qE '[0-9]{3,}' "$FTP_OUT"; then
    pass "UFS SIZE specials.txt"
else
    fail "UFS SIZE specials.txt"
    cat "$FTP_OUT" | sed 's/^/    /'
fi

# --- UFS Cleanup: DELE files, RMD directory ---
info "DELE specials.txt + binary.dat"
FTP_OUT="$TMPDIR/ftp_ufs_dele.log"
ftp_run "$(cat <<CMDS
cd $UFS_TESTDIR
delete specials.txt
delete binary.dat
CMDS
)" "$FTP_OUT"
DELE_COUNT=$(grep -c "250" "$FTP_OUT" || true)
if [ "$DELE_COUNT" -ge 2 ] 2>/dev/null; then
    pass "UFS DELE both files"
else
    grep -qi "250" "$FTP_OUT" && pass "UFS DELE (partial)" || fail "UFS DELE"
    cat "$FTP_OUT" | sed 's/^/    /'
fi

# RMD — remove test directory
info "RMD $UFS_TESTDIR"
FTP_OUT="$TMPDIR/ftp_ufs_rmd.log"
ftp_run "$(cat <<CMDS
cd /
rmdir $UFS_TESTDIR
CMDS
)" "$FTP_OUT"
if grep -qi "250\|Directory removed" "$FTP_OUT"; then
    pass "RMD $UFS_TESTDIR"
else
    fail "RMD $UFS_TESTDIR"
    cat "$FTP_OUT" | sed 's/^/    /'
fi

fi  # UFS_AVAILABLE

# ============================================================
# TEST 5c: Hybrid Navigation (Phase 3c) — only if UFSD running
# ============================================================
if [ "${UFS_AVAILABLE:-0}" = "1" ]; then
section "Test 5c: Hybrid MVS/UFS/JES Navigation"

# Full hybrid cycle in a single session:
# MVS → UFS → MVS → JES → back to MVS
info "Hybrid: MVS → UFS → MVS → JES → MVS"
FTP_OUT="$TMPDIR/ftp_hybrid.log"
ftp_run "$(cat <<CMDS
pwd
cd /
pwd
cd '${HLQ}.'
pwd
site filetype=jes
site filetype=seq
pwd
CMDS
)" "$FTP_OUT"

# Check MVS mode at start
if grep -qi "257.*${HLQ}\.\|working directory.*${HLQ}" "$FTP_OUT"; then
    pass "Hybrid: starts in MVS mode"
else
    fail "Hybrid: expected MVS mode at start"
    cat "$FTP_OUT" | sed 's/^/    /'
fi

# Check UFS mode after CWD /
if grep -qi "250.*HFS directory.*/" "$FTP_OUT"; then
    pass "Hybrid: CWD / switches to UFS"
else
    fail "Hybrid: CWD / did not switch to UFS"
fi

# Check MVS mode after CWD 'HLQ.'
if grep -qi "250.*${HLQ}\.\|working directory.*${HLQ}" "$FTP_OUT"; then
    pass "Hybrid: CWD 'HLQ.' switches back to MVS"
else
    fail "Hybrid: CWD 'HLQ.' did not switch to MVS"
fi

# SITE FILETYPE=JES from UFS mode, then back
info "Hybrid: JES from UFS mode preserves fsmode"
FTP_OUT="$TMPDIR/ftp_hybrid_jes.log"
ftp_run "$(cat <<CMDS
cd /
pwd
site filetype=jes
site filetype=seq
pwd
CMDS
)" "$FTP_OUT"

# After JES→SEQ, PWD should show UFS (restored from before JES)
if grep -qi 'HFS.*working directory\|"/"' "$FTP_OUT"; then
    pass "Hybrid: FILETYPE=SEQ restores UFS mode"
else
    # Check if at least the commands succeeded
    info "Hybrid JES→SEQ output:"
    grep -i "250\|257\|200\|215" "$FTP_OUT" | sed 's/^/    /'
    fail "Hybrid: FILETYPE=SEQ did not restore UFS mode"
fi

fi  # UFS_AVAILABLE (hybrid)

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
# TEST 7: Out of space (x37) — ftpd#129
#
# An upload that does not fit must fail PERMANENTLY, and must not leave the
# session hanging.  Both were wrong before:
#
#   - recovery answered 451, which RFC 959 defines as transient, so a
#     conforming client retries something that cannot succeed;
#   - recovery ABENDed a second time in fclose() and never reached the data
#     connection close, so the client sat writing into a dead transfer until
#     the idle timeout fired 300 seconds later.
#
# Since libc370 1.0.6 the out-of-space condition is not an ABEND at all
# (libc370#176): it arrives as a failed write with errno ENOSPC, so the 552
# now comes from STOR's own error path rather than from the ESTAE, and the
# partial data set can be — and is — scratched (ftpd#135, ftpd#127).
#
# TRK(1,0) has no secondary extents, so ~700 KB cannot fit however the
# volume is laid out.  SECONDARY=0 must reach the allocation as zero for
# this to be a one-track data set at all (ftpd#100) — run this against a
# build without that fix and the 0 becomes 50, the data set becomes 51
# tracks, the upload succeeds and this whole section measures nothing.
# ============================================================
section "Test 7: Out of space (x37) — permanent failure, no hang"

X37FILE="$TMPDIR/test_x37.bin"
DSN_X37="${HLQ}.TEST.X37"

generate_binary_testfile "$X37FILE" 720000
X37SIZE=$(stat -c%s "$X37FILE" 2>/dev/null || stat -f%z "$X37FILE")
info "Generated $X37SIZE bytes for a TRK(1,0) data set"

# A leftover from an earlier run would make this measure the wrong thing:
# STOR skips the allocation entirely when the data set already exists.
ftp_run "delete '$DSN_X37'" "$TMPDIR/ftp_x37_pre.log"

info "STOR into TRK(1,0) -> '$DSN_X37' (expect 552, promptly)"
FTP_OUT="$TMPDIR/ftp_x37_up.log"
X37_START=$(date +%s)
ftp_run "$(cat <<CMDS
site recfm=fb
site lrecl=80
site blksize=3120
site tracks
site primary=1
site secondary=0
type binary
put $X37FILE '$DSN_X37'
CMDS
)" "$FTP_OUT"
X37_ELAPSED=$(( $(date +%s) - X37_START ))

if grep -q "^552" "$FTP_OUT"; then
    pass "Out-of-space upload answers 552 (permanent)"
elif grep -q "^451" "$FTP_OUT"; then
    fail "Out-of-space upload answers 451 (transient — client will retry)"
    grep -E "^4[0-9][0-9]|^5[0-9][0-9]" "$FTP_OUT" | tail -3 | sed 's/^/    /'
else
    fail "Out-of-space upload did not fail as expected"
    tail -5 "$FTP_OUT" | sed 's/^/    /'
fi

# The regression this guards: recovery used to ABEND again in fclose() and
# skip the data connection close, leaving the client to time out.  30s is
# far above a healthy run (~2s) and far below the 300s idle timeout.
if [ "$X37_ELAPSED" -lt 30 ] && ! grep -q "^421" "$FTP_OUT"; then
    pass "Session ended promptly (${X37_ELAPSED}s) — data connection was closed"
else
    fail "Session hung for ${X37_ELAPSED}s — recovery did not close the data connection"
    grep -E "^421" "$FTP_OUT" | sed 's/^/    /'
fi

# The partial data set must be gone: DISP=(NEW,CATLG,DELETE) says so, and
# until libc370 1.0.6 nothing performed it.  A leftover is not cosmetic — the
# next STOR finds it, skips the allocation entirely and fails identically
# whatever the client asks for with SITE.
FTP_OUT="$TMPDIR/ftp_x37_gone.log"
ftp_run "delete '$DSN_X37'" "$FTP_OUT"
if grep -qi "does not exist" "$FTP_OUT"; then
    pass "Partial data set was scratched"
elif grep -qi "IDCAMS rc=8" "$FTP_OUT"; then
    fail "Partial data set survives, locked by the address space (libc370#168 —"
    fail "  is the sysroot older than 1.0.6?)"
else
    fail "Partial data set was not scratched"
    tail -2 "$FTP_OUT" | sed 's/^/    /'
fi

# And the retry must now get a clean allocation rather than inheriting the
# leftover — same failure, reached the same way, not a different code path.
info "Second attempt — must fail the same way, from a fresh allocation"
FTP_OUT="$TMPDIR/ftp_x37_retry.log"
ftp_run "$(cat <<CMDS
site recfm=fb
site lrecl=80
site blksize=3120
site tracks
site primary=1
site secondary=0
type binary
put $X37FILE '$DSN_X37'
CMDS
)" "$FTP_OUT"
if grep -q "^552" "$FTP_OUT"; then
    pass "Retry answers 552 from a fresh allocation"
else
    fail "Retry did not answer 552"
    tail -5 "$FTP_OUT" | sed 's/^/    /'
fi

ftp_run "delete '$DSN_X37'" "$TMPDIR/ftp_x37_post.log"

# ============================================================
# TEST 8: A failed member STOR into a full PDS
#
# A member of 47 records is smaller than one 19040-byte block, so it
# reaches DASD only inside CLOSE, and an x37 there is reported by
# fclose() alone.  Members are stored until one fails; how many fit
# depends on the volume, so the loop finds out.  Five directory blocks
# so that the space runs out before the directory does: one block holds
# 20 members, and on a 3390 two tracks hold more than that -- the 21st
# member then fails on the directory (an ABEND in CLOSE, 451), and the
# overwrite below succeeds, which measures nothing (dev1, 2026-10-13).  Then the PDS is full,
# and three things must hold:
#
#   - the STOR that failed answered 552, not 250;
#   - the member it was storing is not in the directory;
#   - a failed overwrite of an existing member leaves its old content.
#
# The first needs FTPD to check fclose(); the other two need libc370
# 2.10.3, whose CLOSE of a failed member skips the STOW (libc370#524).
# Before that a failed new member stayed with no records and a failed
# overwrite left the old member empty.  The PDS has to be filled by
# SUCCESSFUL members: one filled by a failed STOR proves nothing about
# the next one.  The shape is mvsMF's curl-nospace section 5.
# ============================================================
section "Test 8: Failed member STOR into a full PDS"

DSN_FULL="${HLQ}.TEST.FULLPDS"
MEMRECS=47

# xfer_reply <log>: the reply that ended the transfer -- not the 221 of QUIT
xfer_reply() {
    grep -E "^(226|250|[45][0-9][0-9]) " "$1" | tail -1
}

# mkmem <file> <tag>: MEMRECS records, each starting with the tag
mkmem() {
    awk -v n=$MEMRECS -v t="$2" \
        'BEGIN{for(i=1;i<=n;i++) printf "%s%06d\n", t, i}' > "$1"
}

info "MKD '$DSN_FULL' (FB 80, BLKSIZE 19040, TRK(2,0), 5 directory blocks)"
FTP_OUT="$TMPDIR/ftp_full_mkd.log"
ftp_run "$(cat <<CMDS
rmdir '$DSN_FULL'
site recfm=fb
site lrecl=80
site blksize=19040
site tracks
site primary=2
site secondary=0
site directory=5
mkdir '$DSN_FULL'
CMDS
)" "$FTP_OUT"

if ! grep -q "^257" "$FTP_OUT"; then
    fail "MKD '$DSN_FULL' failed"
    tail -5 "$FTP_OUT" | sed 's/^/    /'
else
    FULL=0
    FULL_REPLY=""
    m=1
    while [ "$m" -le 80 ]; do
        mkmem "$TMPDIR/full_m$m.txt" "MEMBER$m-"
        FTP_OUT="$TMPDIR/ftp_full_m$m.log"
        ftp_run "type ascii
put $TMPDIR/full_m$m.txt '$DSN_FULL(M$m)'" "$FTP_OUT"
        if ! grep -q "^226\|^250" "$FTP_OUT"; then
            FULL=$m
            FULL_REPLY=$(xfer_reply "$FTP_OUT")
            break
        fi
        m=$((m + 1))
    done

    if [ "$FULL" -eq 0 ]; then
        fail "The PDS did not fill within 80 members (or every failure answered 2xx)"
    elif [ "$FULL" -eq 1 ]; then
        fail "The first member into the empty PDS failed: $FULL_REPLY"
    else
        info "Filled: M1..M$((FULL - 1)) stored, M$FULL answered: $FULL_REPLY"

        if echo "$FULL_REPLY" | grep -q "^552"; then
            pass "A member STOR into the full PDS answers 552"
        else
            fail "A member STOR into the full PDS answered: $FULL_REPLY"
        fi

        # A member whose CLOSE failed must not be listed.  RETR of a
        # member in the directory with no records would answer 226 and
        # an empty file; one that is absent answers 550.
        FTP_OUT="$TMPDIR/ftp_full_absent.log"
        rm -f "$TMPDIR/full_absent.txt"
        ftp_run "type ascii
get '$DSN_FULL(M$FULL)' $TMPDIR/full_absent.txt" "$FTP_OUT"
        if grep -q "^550" "$FTP_OUT"; then
            pass "The member whose STOR failed (M$FULL) is not in the directory"
        else
            fail "The member whose STOR failed (M$FULL) is in the directory ($(wc -l < "$TMPDIR/full_absent.txt" 2>/dev/null || echo 0) records)"
            tail -3 "$FTP_OUT" | sed 's/^/    /'
        fi

        # The overwrite of an existing member that does not fit any more
        mkmem "$TMPDIR/full_r1.txt" "REPLACE1-"
        FTP_OUT="$TMPDIR/ftp_full_r1.log"
        ftp_run "type ascii
put $TMPDIR/full_r1.txt '$DSN_FULL(M1)'" "$FTP_OUT"
        R1_REPLY=$(xfer_reply "$FTP_OUT")

        FTP_OUT="$TMPDIR/ftp_full_m1back.log"
        rm -f "$TMPDIR/full_m1back.txt"
        ftp_run "type ascii
get '$DSN_FULL(M1)' $TMPDIR/full_m1back.txt" "$FTP_OUT"
        OLD=$(grep -c "^MEMBER1-" "$TMPDIR/full_m1back.txt" 2>/dev/null)
        NEW=$(grep -c "^REPLACE1-" "$TMPDIR/full_m1back.txt" 2>/dev/null)
        OLD=${OLD:-0}
        NEW=${NEW:-0}

        if echo "$R1_REPLY" | grep -q "^2"; then
            fail "An overwrite into the full PDS answered: $R1_REPLY ($NEW new records) -- the PDS was not full, so this proved nothing"
        elif ! echo "$R1_REPLY" | grep -q "^552"; then
            fail "An overwrite into the full PDS answered: $R1_REPLY (expected 552)"
        else
            pass "An overwrite into the full PDS answers 552"
        fi
        if [ "$OLD" -eq "$MEMRECS" ] && [ "$NEW" -eq 0 ]; then
            pass "The failed overwrite left M1 with its $MEMRECS old records"
        else
            fail "The failed overwrite left M1 with $OLD old and $NEW new records (libc370#524: is the sysroot older than 2.10.3?)"
        fi
    fi
fi

ftp_run "rmdir '$DSN_FULL'" "$TMPDIR/ftp_full_post.log"

# ============================================================
# TEST 9: APPE
#
# STOR replaces, APPE adds.  What each case has to show:
#   - an existing sequential data set keeps its old records and gets the
#     new ones after them (before the change APPE replaced them);
#   - a data set or member that is not there yet is created;
#   - an existing PDS member is refused with 550 and is unchanged afterwards
#     (BPAM cannot extend a member; libc370#204);
#   - UFS still answers 502.
# ============================================================
section "Test 9: APPE"

DSN_APP="${HLQ}.TEST.APPESEQ"
DSN_APPN="${HLQ}.TEST.APPENEW"
DSN_APPP="${HLQ}.TEST.APPEPDS"
APPRECS=23

# mkapp <file> <tag>: APPRECS records, each starting with the tag
mkapp() {
    awk -v n=$APPRECS -v t="$2" \
        'BEGIN{for(i=1;i<=n;i++) printf "%s%06d\n", t, i}' > "$1"
}

# fetch <dsn> <local>: RETR as text, trailing blanks of the FB records cut
fetch() {
    rm -f "$2.raw"
    ftp_run "type ascii
get '$1' $2.raw" "$TMPDIR/ftp_fetch.log"
    if [ -f "$2.raw" ]; then
        sed 's/[[:space:]]*$//' "$2.raw" | tr -d '\r' > "$2"
    else
        : > "$2"
    fi
}

mkapp "$TMPDIR/app_a.txt" "FIRST-"
mkapp "$TMPDIR/app_b.txt" "SECOND-"
cat "$TMPDIR/app_a.txt" "$TMPDIR/app_b.txt" > "$TMPDIR/app_ab.txt"

ftp_run "delete '$DSN_APP'
delete '$DSN_APPN'
rmdir '$DSN_APPP'" "$TMPDIR/ftp_app_pre.log"

# --- 9a: existing sequential data set -------------------------------
info "STOR then APPE -> '$DSN_APP'"
FTP_OUT="$TMPDIR/ftp_app_seq.log"
ftp_run "$(cat <<CMDS
site recfm=fb
site lrecl=80
site blksize=3120
site primary=2
site secondary=1
site tracks
type ascii
put $TMPDIR/app_a.txt '$DSN_APP'
append $TMPDIR/app_b.txt '$DSN_APP'
CMDS
)" "$FTP_OUT"
APP_REPLY=$(xfer_reply "$FTP_OUT")
if grep -q "^125 Appending to data set" "$FTP_OUT" && echo "$APP_REPLY" | grep -q "^2"; then
    pass "APPE to an existing sequential data set answers 125 Appending, then 2xx"
else
    fail "APPE to an existing sequential data set: $APP_REPLY"
    tail -5 "$FTP_OUT" | sed 's/^/    /'
fi

fetch "$DSN_APP" "$TMPDIR/app_seq_back.txt"
if diff -q "$TMPDIR/app_ab.txt" "$TMPDIR/app_seq_back.txt" > /dev/null 2>&1; then
    pass "APPE kept the $APPRECS old records and added $APPRECS after them"
else
    OLD=$(grep -c "^FIRST-" "$TMPDIR/app_seq_back.txt")
    NEW=$(grep -c "^SECOND-" "$TMPDIR/app_seq_back.txt")
    fail "APPE to a sequential data set left $OLD old and $NEW new records (expected $APPRECS and $APPRECS, old first)"
fi

# A second APPE goes behind the first, not over it
FTP_OUT="$TMPDIR/ftp_app_seq2.log"
ftp_run "type ascii
append $TMPDIR/app_a.txt '$DSN_APP'" "$FTP_OUT"
fetch "$DSN_APP" "$TMPDIR/app_seq_back2.txt"
cat "$TMPDIR/app_ab.txt" "$TMPDIR/app_a.txt" > "$TMPDIR/app_aba.txt"
if diff -q "$TMPDIR/app_aba.txt" "$TMPDIR/app_seq_back2.txt" > /dev/null 2>&1; then
    pass "A second APPE appends again ($((APPRECS * 3)) records)"
else
    fail "A second APPE left $(wc -l < "$TMPDIR/app_seq_back2.txt") records (expected $((APPRECS * 3)))"
fi

# --- 9b: data set that does not exist -------------------------------
info "APPE -> '$DSN_APPN' (does not exist)"
FTP_OUT="$TMPDIR/ftp_app_new.log"
ftp_run "$(cat <<CMDS
site recfm=fb
site lrecl=80
site blksize=3120
site primary=2
site secondary=1
site tracks
type ascii
append $TMPDIR/app_a.txt '$DSN_APPN'
CMDS
)" "$FTP_OUT"
APP_REPLY=$(xfer_reply "$FTP_OUT")
fetch "$DSN_APPN" "$TMPDIR/app_new_back.txt"
if echo "$APP_REPLY" | grep -q "^2" \
   && diff -q "$TMPDIR/app_a.txt" "$TMPDIR/app_new_back.txt" > /dev/null 2>&1; then
    pass "APPE to a missing data set creates it with the $APPRECS records"
else
    fail "APPE to a missing data set: $APP_REPLY, $(wc -l < "$TMPDIR/app_new_back.txt") records back"
    tail -5 "$FTP_OUT" | sed 's/^/    /'
fi

# --- 9c/9d: PDS ------------------------------------------------------
info "MKD '$DSN_APPP', STOR member OLD, APPE member NEW and OLD"
FTP_OUT="$TMPDIR/ftp_app_pds.log"
ftp_run "$(cat <<CMDS
site recfm=fb
site lrecl=80
site blksize=3120
site tracks
site primary=2
site secondary=1
site directory=5
mkdir '$DSN_APPP'
type ascii
put $TMPDIR/app_a.txt '$DSN_APPP(OLD)'
append $TMPDIR/app_b.txt '$DSN_APPP(NEW)'
append $TMPDIR/app_b.txt '$DSN_APPP(OLD)'
CMDS
)" "$FTP_OUT"

fetch "$DSN_APPP(NEW)" "$TMPDIR/app_mnew_back.txt"
if diff -q "$TMPDIR/app_b.txt" "$TMPDIR/app_mnew_back.txt" > /dev/null 2>&1; then
    pass "APPE to a missing PDS member creates it"
else
    fail "APPE to a missing PDS member: $(wc -l < "$TMPDIR/app_mnew_back.txt") records back"
    tail -8 "$FTP_OUT" | sed 's/^/    /'
fi

if grep -q "^550 .*OLD" "$FTP_OUT" && grep -qi "unchanged" "$FTP_OUT"; then
    pass "APPE to an existing PDS member answers 550 and says the member is unchanged"
else
    fail "APPE to an existing PDS member did not answer the explanatory 550"
    tail -8 "$FTP_OUT" | sed 's/^/    /'
fi

fetch "$DSN_APPP(OLD)" "$TMPDIR/app_mold_back.txt"
if diff -q "$TMPDIR/app_a.txt" "$TMPDIR/app_mold_back.txt" > /dev/null 2>&1; then
    pass "The refused APPE left the member with its $APPRECS old records"
else
    fail "The refused APPE left OLD with $(grep -c "^FIRST-" "$TMPDIR/app_mold_back.txt") of its $APPRECS old records and $(grep -c "^SECOND-" "$TMPDIR/app_mold_back.txt") of the appended ones"
fi

# --- 9e: UFS ---------------------------------------------------------
if [ "${UFS_AVAILABLE:-0}" = "1" ]; then
    FTP_OUT="$TMPDIR/ftp_app_ufs.log"
    ftp_run "cd /
type ascii
append $TMPDIR/app_a.txt /appe_probe" "$FTP_OUT"
    if grep -q "^502" "$FTP_OUT"; then
        pass "APPE in UFS mode still answers 502"
    else
        fail "APPE in UFS mode did not answer 502"
        tail -4 "$FTP_OUT" | sed 's/^/    /'
    fi
else
    info "UFSD not available -- UFS APPE check skipped"
fi

ftp_run "delete '$DSN_APP'
delete '$DSN_APPN'
rmdir '$DSN_APPP'" "$TMPDIR/ftp_app_post.log"

# ============================================================
# TEST 10: SITE in any spelling (#189, RFC 959 5.3)
#
# The server upper-cases the verb, the keywords and their values, so
# SITE, site and Site are one command.  BSD ftp/tnftp implement `site`
# themselves and refuse `SITE` and `Site` without asking the server, so
# every spelling is sent through `quote`, which passes the line unchanged.
# The per-keyword comparison is test/tstsit.c; this section covers the
# verb and one value that decides what the server does (JESOWNER, read back
# from the 550 that DELE of a job that is not there answers; it needs no data
# connection).
# ============================================================
section "Test 10: SITE in any spelling"

LUSER=$(echo "$USER" | tr '[:upper:]' '[:lower:]')

info "quote SITE / site / Site with upper, lower and mixed case keywords"
FTP_OUT="$TMPDIR/ftp_site_case.log"
ftp_run "$(cat <<CMDS
quote SITE LRECL=80
quote site LRECL=80
quote Site Lrecl=80
quote SITE RECFM=FB
quote site recfm=fb
quote SITE FILETYPE=JES
quote site filetype=seq
quote SITE JESSTATUS=ALL
quote site jesstatus=all
quote sItE TRACKS
quote SITE
CMDS
)" "$FTP_OUT"
SITE_OK=$(grep -c "^200 SITE command was accepted" "$FTP_OUT")
if [ "$SITE_OK" -eq 10 ] && grep -q "^202 SITE not necessary" "$FTP_OUT" \
   && ! grep -q "^500\|Unrecognized" "$FTP_OUT"; then
    pass "SITE, site, Site and sItE with any keyword spelling: 10 x 200, bare SITE 202"
else
    fail "SITE spellings: $SITE_OK of 10 answered 200"
    tail -14 "$FTP_OUT" | sed 's/^/    /'
fi

info "JESOWNER in upper and lower case"
FTP_OUT="$TMPDIR/ftp_site_owner.log"
ftp_run "$(cat <<CMDS
quote SITE FILETYPE=JES
quote SITE JESOWNER=$HLQ
quote DELE JOB99999
quote site jesowner=$LUSER
quote DELE JOB99999
quote SITE FILETYPE=SEQ
CMDS
)" "$FTP_OUT"
OWN_UP=$(grep -c "^550 .*JESOWNER=$HLQ\$" "$FTP_OUT")
OWN_ANY=$(grep -c "^550 Jobid JOB99999 not found for JESJOBNAME=" "$FTP_OUT")
if [ "$OWN_ANY" -eq 2 ] && [ "$OWN_UP" -eq 2 ]; then
    pass "jesowner=$LUSER filters the same owner as JESOWNER=$HLQ"
else
    fail "JESOWNER spelling: $OWN_UP of $OWN_ANY JES replies named JESOWNER=$HLQ"
    grep "^550" "$FTP_OUT" | sed 's/^/    /'
fi

# ============================================================
# TEST 11: A member STOR into a PDS whose DIRECTORY is full (#173)
#
# Test 8 keeps the directory out of the way on purpose; this is the
# opposite shape: one directory block (20 members) and room for far
# more members than that, so the 21st STOR fails on the directory and
# on nothing else.  The system ABENDs the CLOSE with SB14-0C (STOW finds
# no room), which is not an x37 and used to answer 451 -- a transient
# code, so a client retried into the same full directory.  A directory
# cannot be extended, so the answer is the permanent 552, with the one
# thing the client can do about it: SITE DIRECTORY= when the PDS is
# created.  Five things must hold:
#
#   - the STOR that failed answered 552 and names SITE DIRECTORY=;
#   - the member it was storing is not in the directory;
#   - the members that were stored are intact;
#   - the server carries on: the same failure again answers 552 again;
#   - the failed CLOSE left no DD behind (#223): the full PDS can be
#     renamed (RNFR/RNTO), its old name can be created again (MKD) and
#     deleted (RMD), and the renamed PDS can be deleted too.  A DD still
#     holding the data set makes the MKD answer 550 (SVC 99 0448).
# ============================================================
section "Test 11: Member STOR into a PDS with a full directory"

DSN_DIR="${HLQ}.TEST.FULLDIR"
DSN_DIR_OLD="${HLQ}.TEST.FULLDOLD"
DSN_DIR_FULL="$DSN_DIR"     # the full PDS, under whichever name it has now

info "MKD '$DSN_DIR' (FB 80, BLKSIZE 19040, TRK(5,0), 1 directory block)"
FTP_OUT="$TMPDIR/ftp_dir_mkd.log"
ftp_run "$(cat <<CMDS
rmdir '$DSN_DIR_OLD'
rmdir '$DSN_DIR'
site recfm=fb
site lrecl=80
site blksize=19040
site tracks
site primary=5
site secondary=0
site directory=1
mkdir '$DSN_DIR'
CMDS
)" "$FTP_OUT"

if ! grep -q "^257" "$FTP_OUT"; then
    fail "MKD '$DSN_DIR' failed"
    tail -5 "$FTP_OUT" | sed 's/^/    /'
else
    DFULL=0
    DFULL_REPLY=""
    m=1
    while [ "$m" -le 60 ]; do
        mkmem "$TMPDIR/dir_m$m.txt" "MEMBER$m-"
        FTP_OUT="$TMPDIR/ftp_dir_m$m.log"
        ftp_run "type ascii
put $TMPDIR/dir_m$m.txt '$DSN_DIR(M$m)'" "$FTP_OUT"
        if ! grep -q "^226\|^250" "$FTP_OUT"; then
            DFULL=$m
            DFULL_REPLY=$(xfer_reply "$FTP_OUT")
            break
        fi
        m=$((m + 1))
    done

    if [ "$DFULL" -eq 0 ]; then
        fail "The directory did not fill within 60 members (or every failure answered 2xx)"
    elif [ "$DFULL" -eq 1 ]; then
        fail "The first member into the empty PDS failed: $DFULL_REPLY"
    else
        info "Filled: M1..M$((DFULL - 1)) stored, M$DFULL answered: $DFULL_REPLY"

        if echo "$DFULL_REPLY" | grep -q "^552"; then
            pass "A member STOR into the PDS with a full directory answers 552"
        else
            fail "A member STOR into the PDS with a full directory answered: $DFULL_REPLY"
        fi
        if echo "$DFULL_REPLY" | grep -q "SITE DIRECTORY="; then
            pass "The 552 names SITE DIRECTORY="
        else
            fail "The reply does not name SITE DIRECTORY=: $DFULL_REPLY"
        fi

        FTP_OUT="$TMPDIR/ftp_dir_absent.log"
        rm -f "$TMPDIR/dir_absent.txt"
        ftp_run "type ascii
get '$DSN_DIR(M$DFULL)' $TMPDIR/dir_absent.txt" "$FTP_OUT"
        if grep -q "^550" "$FTP_OUT"; then
            pass "The member whose STOR failed (M$DFULL) is not in the directory"
        else
            fail "The member whose STOR failed (M$DFULL) is in the directory"
            tail -3 "$FTP_OUT" | sed 's/^/    /'
        fi

        # The members before it, first and last, still hold their records
        DINTACT=1
        for k in 1 $((DFULL - 1)); do
            FTP_OUT="$TMPDIR/ftp_dir_back$k.log"
            rm -f "$TMPDIR/dir_back$k.txt"
            ftp_run "type ascii
get '$DSN_DIR(M$k)' $TMPDIR/dir_back$k.txt" "$FTP_OUT"
            GOT=$(grep -c "^MEMBER$k-" "$TMPDIR/dir_back$k.txt" 2>/dev/null)
            if [ "${GOT:-0}" -ne "$MEMRECS" ]; then
                DINTACT=0
                info "M$k has ${GOT:-0} of $MEMRECS records"
            fi
        done
        if [ "$DINTACT" -eq 1 ]; then
            pass "The members stored before the failure are intact"
        else
            fail "A member stored before the failure lost records"
        fi

        # The same failure again, on a new session: the server survived the
        # first one, and the retry meets the same permanent answer.
        mkmem "$TMPDIR/dir_again.txt" "AGAIN-"
        FTP_OUT="$TMPDIR/ftp_dir_again.log"
        ftp_run "type ascii
put $TMPDIR/dir_again.txt '$DSN_DIR(M$DFULL)'" "$FTP_OUT"
        AGAIN_REPLY=$(xfer_reply "$FTP_OUT")
        if echo "$AGAIN_REPLY" | grep -q "^552"; then
            pass "The retry answers 552 again and the server is still serving"
        else
            fail "The retry answered: ${AGAIN_REPLY:-no reply}"
        fi

        # The remedy the 552 offers: move the full PDS away and MKD a bigger
        # one under the old name (#223).  The recovered SB14 used to leave the
        # DD of the abended CLOSE allocated to the old name, and the new
        # allocation of that name was refused (SVC 99 0448) until FTPD was
        # restarted.  Its own sessions, after the 552s above.
        FTP_OUT="$TMPDIR/ftp_dir_ren.log"
        ftp_run "rename '$DSN_DIR' '$DSN_DIR_OLD'" "$FTP_OUT"
        if grep -q "^250" "$FTP_OUT"; then
            pass "RNFR/RNTO of the full PDS after the failure answers 250"
            DSN_DIR_FULL="$DSN_DIR_OLD"

            FTP_OUT="$TMPDIR/ftp_dir_mkd2.log"
            ftp_run "$(cat <<CMDS
site recfm=fb
site lrecl=80
site blksize=19040
site tracks
site primary=5
site secondary=0
site directory=5
mkdir '$DSN_DIR'
CMDS
)" "$FTP_OUT"
            if grep -q "^257" "$FTP_OUT"; then
                pass "MKD of the old name after the rename answers 257"
                FTP_OUT="$TMPDIR/ftp_dir_rmd2.log"
                ftp_run "rmdir '$DSN_DIR'" "$FTP_OUT"
                if grep -q "^250" "$FTP_OUT"; then
                    pass "The PDS created under the old name can be deleted"
                else
                    fail "RMD of the new PDS: $(tail -2 "$FTP_OUT" | tr '\n' ' ')"
                fi
            else
                fail "MKD of the old name after the rename: $(grep -E '^[45][0-9][0-9] ' "$FTP_OUT" | tail -1)"
            fi
        else
            fail "RNFR/RNTO of the full PDS: $(tail -2 "$FTP_OUT" | tr '\n' ' ')"
        fi
    fi
fi

FTP_OUT="$TMPDIR/ftp_dir_post.log"
ftp_run "rmdir '$DSN_DIR_FULL'" "$FTP_OUT"
if grep -q "^250" "$FTP_OUT"; then
    pass "The full PDS can be deleted after the failure (no DD left allocated)"
else
    fail "RMD of the full PDS after the failure: $(tail -2 "$FTP_OUT" | tr '\n' ' ')"
fi

# ============================================================
# Test 12: STOR and APPE on a retained data set (#198)
#
# OPEN for output of a data set whose expiration date has not passed raises
# IEC507D and waits for an operator reply.  FTPD answers 550 with the date
# before the OPEN instead.  Without the check the second STOR below never
# answers; ftp_run_wd cuts such a session off, and the console still shows
# the IEC507D to reply to.
#
# Needs RETPD (SITE RETPD, #130).  Expiration is one day ahead, so the data
# sets are deleted at the end: DELE and RMD do not ask for retention.
# ============================================================
section "Test 12: STOR and APPE on a retained data set"

DSN_RET="${HLQ}.TEST.RETSEQ"
DSN_RETP="${HLQ}.TEST.RETPDS"
printf 'RETAINED-000001\nRETAINED-000002\n' > "$TMPDIR/ret.txt"

# ftp_run, but a session stuck in OPEN (IEC507D) is cut off after 90 s
ftp_run_wd() {
    local cmds="$1"
    local capture="${2:-/dev/null}"

    printf "user %s %s\n%b\nquit\n" "$USER" "$PASS" "$cmds" \
        | timeout 90 ftp -n -v -p -P "$PORT" "$HOST" > "$capture" 2>&1
    return $?
}

ftp_run "delete '$DSN_RET'
rmdir '$DSN_RETP'" /dev/null

info "STOR twice and APPE with RETPD=1 -> '$DSN_RET'"
FTP_OUT="$TMPDIR/ftp_ret_seq.log"
ftp_run_wd "$(cat <<CMDS
site recfm=fb
site lrecl=80
site blksize=3120
site primary=2
site secondary=1
site tracks
site retpd=1
type ascii
put $TMPDIR/ret.txt '$DSN_RET'
put $TMPDIR/ret.txt '$DSN_RET'
append $TMPDIR/ret.txt '$DSN_RET'
CMDS
)" "$FTP_OUT"
RET_REFUSED=$(grep -cE "^550 .* is retained until [0-9]{4}-[0-9]{2}-[0-9]{2} \([0-9]{4}\.[0-9]{3}\)" "$FTP_OUT")
if [ "$RET_REFUSED" -eq 2 ] && grep -q "^250 \|^226 " "$FTP_OUT"; then
    pass "STOR creates the retained data set; STOR and APPE over it answer 550 with the date"
else
    fail "retained data set: $RET_REFUSED of 2 refusals answered 550 with a date (a hang is IEC507D)"
    tail -12 "$FTP_OUT" | sed 's/^/    /'
fi

info "STOR without RETPD over the retained data set"
FTP_OUT="$TMPDIR/ftp_ret_plain.log"
ftp_run_wd "type ascii
put $TMPDIR/ret.txt '$DSN_RET'" "$FTP_OUT"
if grep -qE "^550 .* is retained until " "$FTP_OUT"; then
    pass "a session without RETPD is refused the same way"
else
    fail "STOR over a retained data set without RETPD: no 550 with a date"
    tail -6 "$FTP_OUT" | sed 's/^/    /'
fi

info "STOR of a member into a PDS that MKD created with RETPD=1"
FTP_OUT="$TMPDIR/ftp_ret_pds.log"
ftp_run_wd "$(cat <<CMDS
site recfm=fb
site lrecl=80
site blksize=3120
site primary=2
site secondary=1
site tracks
site retpd=1
mkdir '$DSN_RETP'
type ascii
put $TMPDIR/ret.txt '$DSN_RETP(M1)'
put $TMPDIR/ret.txt '$DSN_RETP(M1)'
CMDS
)" "$FTP_OUT"
RETP_REFUSED=$(grep -cE "^550 .* is retained until " "$FTP_OUT")
if [ "$RETP_REFUSED" -eq 2 ]; then
    pass "STOR into a retained PDS answers 550 with the date, new member or not"
else
    fail "retained PDS: $RETP_REFUSED of 2 member STORs answered 550 with a date"
    tail -8 "$FTP_OUT" | sed 's/^/    /'
fi

info "DELE and RMD of the retained data sets"
FTP_OUT="$TMPDIR/ftp_ret_clean.log"
ftp_run_wd "delete '$DSN_RET'
rmdir '$DSN_RETP'" "$FTP_OUT"
if [ "$(grep -c "^250 .* deleted" "$FTP_OUT")" -eq 2 ]; then
    pass "DELE and RMD remove retained data sets"
else
    fail "DELE/RMD of retained data sets"
    tail -6 "$FTP_OUT" | sed 's/^/    /'
fi

# ============================================================
# Test 13: Byte counters (#206)
#
# F FTPD,STATS (BYTES IN / BYTES OUT) and the sent= / recv= of the
# "session ended" line are the same numbers: ftpd#ses.c adds the session's
# counters to the server totals when the session ends.  Each byte has to be
# counted once, by the data connection functions.  MVS data set transfers were
# counted three times (the session counter twice) until #206.
#
# One session per path, so the change of the totals is exactly what that one
# session transferred: STOR then RETR of a known number of bytes, on an MVS
# data set and on a UFS file.  Each file is a multiple of the record length
# for the MVS case, so RETR sends back the bytes STOR received.
# ============================================================
section "Test 13: Byte counters"

# BYTES IN and BYTES OUT from F FTPD,STATS, as "in out"
ftpd_stats() {
    $FTPD_CONSOLE "F FTPD,STATS" 2>&1 | awk '
        /FTPD012I/ { in_ = $NF }
        /FTPD013I/ { out = $NF }
        END { print in_, out }'
}

# The server adds a session's bytes when the session thread ends, which can
# be a moment after the client has quit: ask until the totals settle on the
# wanted change or the time is up.  Sets STATS_IN / STATS_OUT to the change
# since "$1 $2".
stats_delta() {
    local base_in="${1%% *}" base_out="${1##* }"
    local want_in="$2" want_out="$3" i cur

    STATS_IN=""; STATS_OUT=""
    for i in 1 2 3 4 5 6 7 8 9 10; do
        cur=$(ftpd_stats)
        STATS_IN=$(( ${cur%% *} - base_in ))
        STATS_OUT=$(( ${cur##* } - base_out ))
        if [ "$STATS_IN" -eq "$want_in" ] && [ "$STATS_OUT" -eq "$want_out" ]; then
            return 0
        fi
        sleep 2
    done
    return 1
}

if [ -z "$FTPD_CONSOLE" ]; then
    info "SKIPPED: FTPD_CONSOLE is not set, F FTPD,STATS cannot be read"
else
    # MVS data set: 600 records of 1024 bytes
    DSN_CNT="${HLQ}.TEST.BYTECNT"
    CNT_MVS=614400
    dd if=/dev/urandom of="$TMPDIR/cnt_mvs.dat" bs=1024 count=600 2>/dev/null
    ftp_run "delete '$DSN_CNT'" /dev/null
    CNT_BASE=$(ftpd_stats)
    info "STATS before: BYTES IN/OUT = $CNT_BASE"
    case "$CNT_BASE" in
        *[!0-9\ ]*|""|" "*|*" ") fail "F FTPD,STATS: no byte counters in the answer ('$CNT_BASE')" ;;
    esac

    FTP_OUT="$TMPDIR/ftp_cnt_mvs.log"
    ftp_run "$(cat <<CMDS
site recfm=fb
site lrecl=1024
site blksize=6144
site primary=100
site secondary=50
site tracks
type binary
put $TMPDIR/cnt_mvs.dat '$DSN_CNT'
get '$DSN_CNT' $TMPDIR/cnt_mvs.back
CMDS
)" "$FTP_OUT"
    if [ "$(stat -c%s "$TMPDIR/cnt_mvs.back" 2>/dev/null || stat -f%z "$TMPDIR/cnt_mvs.back" 2>/dev/null)" = "$CNT_MVS" ]; then
        if stats_delta "$CNT_BASE" "$CNT_MVS" "$CNT_MVS"; then
            pass "MVS data set: $CNT_MVS bytes each way counted once (IN $STATS_IN, OUT $STATS_OUT)"
        else
            fail "MVS data set: sent and received $CNT_MVS bytes each way, STATS counted IN $STATS_IN, OUT $STATS_OUT"
        fi
    else
        fail "MVS data set: the $CNT_MVS bytes did not make the round trip"
        tail -5 "$FTP_OUT" | sed 's/^/    /'
    fi
    ftp_run "delete '$DSN_CNT'" /dev/null

    # UFS file: an odd size on purpose
    UFS_CNTDIR="/tmp/ftpdcnt$$"
    CNT_UFS=300001
    dd if=/dev/urandom of="$TMPDIR/cnt_ufs.dat" bs=1 count=$CNT_UFS 2>/dev/null
    ftp_run "cd /
mkdir $UFS_CNTDIR" /dev/null
    CNT_BASE=$(ftpd_stats)

    FTP_OUT="$TMPDIR/ftp_cnt_ufs.log"
    ftp_run "$(cat <<CMDS
cd $UFS_CNTDIR
type binary
put $TMPDIR/cnt_ufs.dat cnt.bin
get cnt.bin $TMPDIR/cnt_ufs.back
CMDS
)" "$FTP_OUT"
    if [ "$(stat -c%s "$TMPDIR/cnt_ufs.back" 2>/dev/null || stat -f%z "$TMPDIR/cnt_ufs.back" 2>/dev/null)" = "$CNT_UFS" ]; then
        if stats_delta "$CNT_BASE" "$CNT_UFS" "$CNT_UFS"; then
            pass "UFS file: $CNT_UFS bytes each way counted once (IN $STATS_IN, OUT $STATS_OUT)"
        else
            fail "UFS file: sent and received $CNT_UFS bytes each way, STATS counted IN $STATS_IN, OUT $STATS_OUT"
        fi
    else
        fail "UFS file: the $CNT_UFS bytes did not make the round trip"
        tail -5 "$FTP_OUT" | sed 's/^/    /'
    fi
    ftp_run "cd $UFS_CNTDIR
delete cnt.bin
cd /
rmdir $UFS_CNTDIR" /dev/null
fi

# ============================================================
# TEST 14: FEAT, SIZE and MDTM in the MVS and the UFS view (#208)
#
# FEAT lists SIZE and MDTM whatever the view is: a session starts in the
# MVS view, clients read FEAT once after the login, and a FEAT that left
# the two out there would hide them from every UFS user.  In the MVS view
# both commands answer 502 (a data set has no exact byte count, #88); in
# the UFS view they answer 213.  This test pins that split, so a change to
# one side has to change the other and the books on purpose.
#
# UFS is asked about the root directory "/": it always exists, and it
# needs no data connection to create a file first.
# ============================================================
section "Test 14: FEAT, SIZE and MDTM in the MVS and the UFS view"

# feat_block <log>: the last FEAT reply in a client log, without CRs
feat_block() {
    tr -d '\r' < "$1" | awk '
        /^211-/ { inb = 1; blk = "" }
        inb     { blk = blk $0 "\n" }
        /^211 / { inb = 0; last = blk }
        END     { printf "%s", last }'
}

FTP_OUT="$TMPDIR/ftp_feat_mvs.log"
ftp_run "$(cat <<CMDS
quote FEAT
quote SIZE ${HLQ}.TEST.NOSUCH
quote MDTM ${HLQ}.TEST.NOSUCH
CMDS
)" "$FTP_OUT"
feat_block "$FTP_OUT" > "$TMPDIR/feat_mvs.txt"

if grep -q '^ SIZE$' "$TMPDIR/feat_mvs.txt" \
   && grep -q '^ MDTM$' "$TMPDIR/feat_mvs.txt"; then
    pass "FEAT in the MVS view lists SIZE and MDTM"
else
    fail "FEAT in the MVS view: SIZE and MDTM not both listed"
    sed 's/^/    /' "$TMPDIR/feat_mvs.txt"
fi
if tr -d '\r' < "$FTP_OUT" | grep -q '^502 SIZE'; then
    pass "SIZE on a data set answers 502"
else
    fail "SIZE on a data set: expected 502"
    tail -5 "$FTP_OUT" | sed 's/^/    /'
fi
if tr -d '\r' < "$FTP_OUT" | grep -q '^502 MDTM'; then
    pass "MDTM on a data set answers 502"
else
    fail "MDTM on a data set: expected 502"
    tail -5 "$FTP_OUT" | sed 's/^/    /'
fi

if [ "${UFS_AVAILABLE:-0}" = "1" ]; then
    FTP_OUT="$TMPDIR/ftp_feat_ufs.log"
    ftp_run "$(cat <<CMDS
cd /
quote FEAT
quote SIZE /
quote MDTM /
CMDS
)" "$FTP_OUT"
    feat_block "$FTP_OUT" > "$TMPDIR/feat_ufs.txt"

    if [ -s "$TMPDIR/feat_ufs.txt" ] \
       && cmp -s "$TMPDIR/feat_mvs.txt" "$TMPDIR/feat_ufs.txt"; then
        pass "FEAT is the same reply in the UFS view as in the MVS view"
    else
        fail "FEAT differs between the MVS and the UFS view"
        diff "$TMPDIR/feat_mvs.txt" "$TMPDIR/feat_ufs.txt" | sed 's/^/    /'
    fi
    if tr -d '\r' < "$FTP_OUT" | grep -Eq '^213 [0-9]+$'; then
        pass "SIZE in the UFS view answers 213"
    else
        fail "SIZE in the UFS view: expected 213 and a number"
        tail -5 "$FTP_OUT" | sed 's/^/    /'
    fi
    if tr -d '\r' < "$FTP_OUT" | grep -Eq '^213 [0-9]{14}$'; then
        pass "MDTM in the UFS view answers 213 and YYYYMMDDHHMMSS"
    else
        fail "MDTM in the UFS view: expected 213 and 14 digits"
        tail -5 "$FTP_OUT" | sed 's/^/    /'
    fi
else
    info "UFS not available, skipping the UFS half of Test 14"
fi

# ============================================================
# TEST 15: Settings that took effect nowhere (#207)
#
# SITE TRUNCATE, SITE RDW, SITE SBSENDEOL, SITE JESSTATUS were accepted with
# 200 and did nothing.  Now:
#   - TRUNCATE decides what STOR does with a text line longer than the
#     record: off, the transfer fails with 451 and a new data set is
#     scratched; on, the line is cut and the 250 says (data was truncated);
#   - RDW and SBSENDEOL other than CRLF are refused with 501;
#   - JESSTATUS filters LIST, RETR and DELE in JES mode.
# The per-keyword replies are in test/tstsit.c; this section is what the
# settings do to a transfer.
# ============================================================
section "Test 15: Settings that took effect nowhere"

DSN_TRN="${HLQ}.TEST.TRUNC"
printf 'short\n0123456789ABCDEF\nlast\n' > "$TMPDIR/trn_long.txt"
printf 'short\n0123456789\nlast\n' > "$TMPDIR/trn_cut.txt"
printf 'short\n0123456789\nlast\n' > "$TMPDIR/trn_fit.txt"

ftp_run "delete '$DSN_TRN'" /dev/null

info "STOR of a 16 byte line into LRECL=10, TRUNCATE off"
FTP_OUT="$TMPDIR/ftp_trn_off.log"
ftp_run "$(cat <<CMDS
site recfm=fb
site lrecl=10
site blksize=100
site primary=1
site secondary=1
site tracks
type ascii
put $TMPDIR/trn_long.txt '$DSN_TRN'
CMDS
)" "$FTP_OUT"
if grep -q "^451 File transfer failed. File contains records that are longer than the LRECL" "$FTP_OUT"; then
    pass "an overlong line fails the STOR with 451"
else
    fail "an overlong line without TRUNCATE: $(xfer_reply "$FTP_OUT")"
    tail -5 "$FTP_OUT" | sed 's/^/    /'
fi
fetch "$DSN_TRN" "$TMPDIR/trn_back0.txt"
if [ ! -s "$TMPDIR/trn_back0.txt" ]; then
    pass "the data set the failed STOR created is gone"
else
    fail "the failed STOR left a data set behind"
fi

info "the same STOR after SITE TRUNCATE"
FTP_OUT="$TMPDIR/ftp_trn_on.log"
ftp_run "$(cat <<CMDS
site recfm=fb
site lrecl=10
site blksize=100
site primary=1
site secondary=1
site tracks
site truncate
type ascii
put $TMPDIR/trn_long.txt '$DSN_TRN'
CMDS
)" "$FTP_OUT"
if grep -q "^250 .*(data was truncated)" "$FTP_OUT"; then
    pass "with TRUNCATE the STOR answers 250 (data was truncated)"
else
    fail "an overlong line with TRUNCATE: $(xfer_reply "$FTP_OUT")"
    tail -5 "$FTP_OUT" | sed 's/^/    /'
fi
fetch "$DSN_TRN" "$TMPDIR/trn_back1.txt"
if diff -q "$TMPDIR/trn_cut.txt" "$TMPDIR/trn_back1.txt" > /dev/null 2>&1; then
    pass "the long line was cut to the LRECL, its neighbours are intact"
else
    fail "TRUNCATE stored: $(tr '\n' '|' < "$TMPDIR/trn_back1.txt")"
fi

info "lines that fit, TRUNCATE on"
FTP_OUT="$TMPDIR/ftp_trn_fit.log"
ftp_run "$(cat <<CMDS
site recfm=fb
site lrecl=10
site blksize=100
site truncate
type ascii
put $TMPDIR/trn_fit.txt '$DSN_TRN'
CMDS
)" "$FTP_OUT"
if grep -q "^250 Transfer completed successfully\.\s*$" "$FTP_OUT" \
   && ! grep -q "truncated" "$FTP_OUT"; then
    pass "lines of exactly LRECL fit: plain 250, no truncation reported"
else
    fail "lines that fit: $(xfer_reply "$FTP_OUT")"
fi
ftp_run "delete '$DSN_TRN'" /dev/null

info "RDW, SBSENDEOL and JESSTATUS"
FTP_OUT="$TMPDIR/ftp_site_dead.log"
ftp_run "$(cat <<CMDS
quote SITE RDW
quote SITE SBSENDEOL=LF
quote SITE SBSENDEOL=CRLF
quote SITE JESSTATUS=HELD
CMDS
)" "$FTP_OUT"
if grep -q "^501 SITE RDW is not supported" "$FTP_OUT"; then
    pass "SITE RDW is refused with 501"
else
    fail "SITE RDW"; tail -6 "$FTP_OUT" | sed 's/^/    /'
fi
# The 200 for SBSENDEOL=CRLF must come right after the 501 for =LF; the
# multi-line JESSTATUS reply further down ends in the same 200 line.
if grep -A1 "^501 SBSENDEOL=LF is not supported" "$FTP_OUT" \
   | grep -q "^200 SITE command was accepted"; then
    pass "SITE SBSENDEOL=LF is refused with 501, SBSENDEOL=CRLF is accepted"
else
    fail "SITE SBSENDEOL"; tail -6 "$FTP_OUT" | sed 's/^/    /'
fi
if grep -q "^200-JESSTATUS must be ALL, INPUT, ACTIVE or OUTPUT" "$FTP_OUT"; then
    pass "SITE JESSTATUS=HELD is refused with a warning"
else
    fail "SITE JESSTATUS=HELD"; tail -6 "$FTP_OUT" | sed 's/^/    /'
fi

info "JESSTATUS filters the JES listing"
printf '//FTPD14 JOB (ACCT),'"'"'FTPD TEST'"'"',CLASS=A,MSGCLASS=X\n//STEP1 EXEC PGM=IEFBR14\n' \
    > "$TMPDIR/jfl_job.jcl"
FTP_OUT="$TMPDIR/ftp_jfl_sub.log"
ftp_run "site filetype=jes
type ascii
put $TMPDIR/jfl_job.jcl jfl_job.jcl" "$FTP_OUT"
JFL_ID=$(grep -o 'JOB[0-9][0-9][0-9][0-9][0-9]' "$FTP_OUT" | head -1)
sleep 8
FTP_OUT="$TMPDIR/ftp_jfl_all.log"
ftp_run "site filetype=jes
site jesowner=*
ls" "$FTP_OUT"
FTP_OUT2="$TMPDIR/ftp_jfl_act.log"
ftp_run "site filetype=jes
site jesowner=*
site jesstatus=active
ls
quote RETR $JFL_ID
quote DELE $JFL_ID" "$FTP_OUT2"
FTP_OUT3="$TMPDIR/ftp_jfl_out.log"
ftp_run "site filetype=jes
site jesowner=*
site jesstatus=output
ls" "$FTP_OUT3"
if [ -n "$JFL_ID" ] && grep -q "$JFL_ID" "$FTP_OUT" \
   && grep -q "JESSTATUS=ALL" "$FTP_OUT"; then
    pass "JESSTATUS=ALL (the default) lists the job, the 125 says ALL"
else
    fail "JESSTATUS=ALL did not list $JFL_ID"; tail -6 "$FTP_OUT" | sed 's/^/    /'
fi
if ! grep -v "^550" "$FTP_OUT2" | grep -q "$JFL_ID" \
   && grep -q "List started OK.*JESSTATUS=ACTIVE" "$FTP_OUT2"; then
    pass "JESSTATUS=ACTIVE does not list a finished job, the 125 says ACTIVE"
else
    fail "JESSTATUS=ACTIVE listed the finished job"; tail -8 "$FTP_OUT2" | sed 's/^/    /'
fi
if [ "$(grep -c "^550 Jobid $JFL_ID not found for .*JESSTATUS=ACTIVE" "$FTP_OUT2")" -eq 2 ]; then
    pass "RETR and DELE of the finished job answer 550 under JESSTATUS=ACTIVE"
else
    fail "RETR/DELE under JESSTATUS=ACTIVE"; tail -8 "$FTP_OUT2" | sed 's/^/    /'
fi
if grep -q "$JFL_ID" "$FTP_OUT3" && grep -q "JESSTATUS=OUTPUT" "$FTP_OUT3"; then
    pass "JESSTATUS=OUTPUT lists the finished job"
else
    fail "JESSTATUS=OUTPUT did not list $JFL_ID"; tail -6 "$FTP_OUT3" | sed 's/^/    /'
fi
[ -n "$JFL_ID" ] && ftp_run "site filetype=jes
quote DELE $JFL_ID" /dev/null

# ============================================================
# TEST 16: Text STOR and RETR in RECFM=VB, VBA and U (#185, #245)
#
# A variable-length data set holds records of different lengths, and a text
# RETR has to give one line per record.  The binary stream libc370 hands out
# for fopen "rb" runs on across record boundaries on RECFM=V, so reading with
# fread(lrecl) used to concatenate the records and re-wrap them at LRECL
# (15 lines came back as 3).  FB is the control: it never had the problem.
# The lines differ in length, one is empty, one is a single character and one
# is the longest the record takes (LRECL 80 less the 4-byte RDW).
#
# RECFM=U has no records of its own, only blocks, and a "rb" stream on it runs
# on across the block boundaries just as on V (#245).  A text STOR writes one
# block per line; a text RETR has to give one line per block.  U has no LRECL
# (the block size is the limit), so none is requested for it.
# ============================================================
section "Test 16: Text STOR and RETR in RECFM=VB, VBA and U"

VBFILE="$TMPDIR/test_vb.txt"
awk 'BEGIN{
    for (i = 1; i <= 15; i++) {
        if (i == 4)       { print ""; continue }
        if (i == 9)       { print "Z"; continue }
        n = (i == 15) ? 76 : i * 5
        s = sprintf("L%02d:", i)
        while (length(s) < n) s = s "x"
        print s
    }
}' > "$VBFILE"
VBLINES=$(wc -l < "$VBFILE")

for RF in FB VB VBA U; do
    DSN_VB="${HLQ}.TEST.TXT${RF}"
    VBBACK="$TMPDIR/test_vb_back_$RF.txt"
    rm -f "$VBBACK"
    ftp_run "delete '$DSN_VB'" /dev/null

    if [ "$RF" = "U" ]; then VBLRECL="site blksize=3120"; else VBLRECL="site lrecl=80"; fi

    info "STOR (TYPE A) $VBLINES lines -> '$DSN_VB' RECFM=$RF"
    FTP_OUT="$TMPDIR/ftp_vb_up_$RF.log"
    ftp_run "$(cat <<CMDS
site recfm=$RF
$VBLRECL
site blksize=3120
site tracks
site primary=1
site secondary=1
type ascii
put $VBFILE '$DSN_VB'
CMDS
)" "$FTP_OUT"
    if grep -q "^250" "$FTP_OUT"; then
        pass "Text STOR into RECFM=$RF"
    else
        fail "Text STOR into RECFM=$RF"
        tail -5 "$FTP_OUT" | sed 's/^/    /'
    fi

    FTP_OUT="$TMPDIR/ftp_vb_dn_$RF.log"
    ftp_run "type ascii
get '$DSN_VB' $VBBACK" "$FTP_OUT"
    if [ -f "$VBBACK" ]; then
        BACKLINES=$(tr -d '\r' < "$VBBACK" | wc -l)
        if [ "$BACKLINES" -eq "$VBLINES" ]; then
            pass "RECFM=$RF: $VBLINES lines in, $BACKLINES lines out"
        else
            fail "RECFM=$RF: $VBLINES lines in, $BACKLINES lines out"
        fi
        if tr -d '\r' < "$VBBACK" | diff -q "$VBFILE" - > /dev/null 2>&1; then
            pass "RECFM=$RF: text reads back line for line"
        else
            fail "RECFM=$RF: text differs after the roundtrip"
            tr -d '\r' < "$VBBACK" | diff -u "$VBFILE" - | head -20 | sed 's/^/    /'
        fi
    else
        fail "RECFM=$RF: text RETR produced no file"
        tail -5 "$FTP_OUT" | sed 's/^/    /'
    fi

    ftp_run "delete '$DSN_VB'" /dev/null
done

# ============================================================
# TEST 17: SITE DCBDSN takes RECFM, LRECL and BLKSIZE from a model (#180)
#
# The model is made with explicit SITE values that differ from every default
# (VB 255 6233 against FB 80 3120).  A data set made after SITE DCBDSN=model
# must carry the model's three values; an unknown model is refused with 501
# and the next data set gets the defaults; a SITE LRECL given after DCBDSN
# wins for LRECL only, and DCBDSN given after LRECL wins again.  The LIST line
# is the read-back:  Volume Unit Referred Ext Used Recfm Lrecl BlkSz Dsorg Dsname
# ============================================================
section "Test 17: SITE DCBDSN"

DCB_MODEL="${HLQ}.TEST.DCBMOD"
DCB_NEW="${HLQ}.TEST.DCBNEW"
DCB_TEXT="$TMPDIR/dcb_text.txt"
printf 'one\ntwo\nthree\n' > "$DCB_TEXT"

# "RECFM LRECL BLKSIZE" of a data set, as its LIST line shows them.
dcb_attrs() {
    local rel="${1#${HLQ}.}"
    ftp_run "ls $rel" "$TMPDIR/dcb_ls.log"
    awk -v d="$rel" '$NF == d { print $6, $7, $8 }' "$TMPDIR/dcb_ls.log" | head -1
}

# Replace data set $1 by a small text STOR made after the SITE commands in $2.
dcb_store() {
    ftp_run "delete '$1'" /dev/null
    ftp_run "site tracks
site primary=1
site secondary=1
$2
type ascii
put $DCB_TEXT '$1'" "$TMPDIR/dcb_stor.log"
}

dcb_store "$DCB_MODEL" "site recfm=VB
site lrecl=255
site blksize=6233"
DCB_GOT=$(dcb_attrs "$DCB_MODEL")
if [ "$DCB_GOT" = "VB 255 6233" ]; then
    pass "the model data set exists as VB 255 6233"
else
    fail "model data set attributes: '$DCB_GOT'"
fi

dcb_store "$DCB_NEW" "site dcbdsn=$DCB_MODEL"
DCB_GOT=$(dcb_attrs "$DCB_NEW")
if [ "$DCB_GOT" = "VB 255 6233" ] \
   && grep -q '^200 .*DCBDSN\|^200 SITE' "$TMPDIR/dcb_stor.log"; then
    pass "SITE DCBDSN=model: a new data set is VB 255 6233"
else
    fail "SITE DCBDSN=model gave '$DCB_GOT'"; tail -6 "$TMPDIR/dcb_stor.log" | sed 's/^/    /'
fi

dcb_store "$DCB_NEW" "site dcbdsn='$DCB_MODEL'"
DCB_GOT=$(dcb_attrs "$DCB_NEW")
if [ "$DCB_GOT" = "VB 255 6233" ]; then
    pass "SITE DCBDSN='model' (quoted) is accepted"
else
    fail "SITE DCBDSN='model' gave '$DCB_GOT'"
fi

dcb_store "$DCB_NEW" "site dcbdsn=$DCB_MODEL
site lrecl=100"
DCB_GOT=$(dcb_attrs "$DCB_NEW")
if [ "$DCB_GOT" = "VB 100 6233" ]; then
    pass "SITE LRECL after DCBDSN wins for LRECL; RECFM and BLKSIZE stay"
else
    fail "DCBDSN then LRECL=100 gave '$DCB_GOT'"
fi

dcb_store "$DCB_NEW" "site lrecl=100
site dcbdsn=$DCB_MODEL"
DCB_GOT=$(dcb_attrs "$DCB_NEW")
if [ "$DCB_GOT" = "VB 255 6233" ]; then
    pass "SITE DCBDSN after LRECL wins: the last one given counts"
else
    fail "LRECL=100 then DCBDSN gave '$DCB_GOT'"
fi

dcb_store "$DCB_NEW" "site dcbdsn=${HLQ}.TEST.NOSUCH"
DCB_GOT=$(dcb_attrs "$DCB_NEW")
if grep -q "^501 DCBDSN: ${HLQ}.TEST.NOSUCH is not cataloged" "$TMPDIR/dcb_stor.log" \
   && [ "$DCB_GOT" = "FB 80 3120" ]; then
    pass "a model that is not cataloged: 501, the data set gets the defaults"
else
    fail "unknown model: '$DCB_GOT'"; tail -6 "$TMPDIR/dcb_stor.log" | sed 's/^/    /'
fi

dcb_store "$DCB_NEW" "site dcbdsn=$DCB_MODEL
site dcbdsn"
DCB_GOT=$(dcb_attrs "$DCB_NEW")
if [ "$DCB_GOT" = "FB 80 3120" ]; then
    pass "a bare SITE DCBDSN returns to the defaults"
else
    fail "bare DCBDSN gave '$DCB_GOT'"
fi

for DCB_BAD in "${HLQ}.TEST.*" "${HLQ}.TEST.DCBMOD(MEM)" "${HLQ}..TEST"; do
    ftp_run "site dcbdsn=$DCB_BAD" "$TMPDIR/dcb_bad.log"
    if grep -q "^501 DCBDSN: " "$TMPDIR/dcb_bad.log"; then
        pass "SITE DCBDSN=$DCB_BAD is refused with 501"
    else
        fail "SITE DCBDSN=$DCB_BAD"; tail -3 "$TMPDIR/dcb_bad.log" | sed 's/^/    /'
    fi
done

ftp_run "delete '$DCB_MODEL'
delete '$DCB_NEW'" /dev/null

# ============================================================
# Test 18: JES spool authorization (#90)
#
# Who may list, read and delete which job in the JES mode of FTP.  The model
# (plan MVS-240, variant 3): a job of the session's own userid is always
# allowed; a job of another owner needs the RACF class JESSPOOL, entity
# <owner>.<jobname>.<jobid>: READ lists and reads, UPDATE or better also
# deletes, no profile or no READ hides the job behind the same 550 that a
# missing job gets.
#
# Needs RAKF 2.0.0 (the test refuses to run a gate on anything else) and a
# test user that is a RAKF administrator (group RAKFADM, SPECIAL), such as
# the IBMUSER of an mvsce container: the test creates its own users and
# JESSPOOL profiles and puts SYS1.SECURE.CNTL(PROFILES) and (USERS) back the
# way it found them.  Use a throwaway container, never a real system.
#
# Fixtures are written as files (the lines of the examples A, B, D, E and F
# of the plan, section 3.3) and loaded with the batch procedures RAKFUSER and
# RAKFPROF.  Every example is proven once first: its command sequence
# (RDEF, PE) is run in batch and the JESSPOOL lines it produces are compared
# with the lines the fixture file uses.  A proof that fails is a FINDING
# about the examples, not a failure of the server; it makes a gate run fail.
#
# Environment (all optional):
#   FTPD_GATE=1       release gate: RAKF other than 2.0.0, an unknown image or
#                     libc370 version and every FINDING make the run fail
#   FTPD_IMAGE        image with digest of the container, for the report
#   FTPD_LIBC370      libc370 version, when FTPD_CONSOLE cannot tell
#   FTPD_RDR          host:port of the card reader, for the job without owner
#   FTPD_CONSOLE      as for Test 13; also checks the console reload (22)
#
# Case 1 (empty JESSPOOL class) runs on every RAKF level and before every
# libc370 raise: if racf_auth() answers 0 for a class without profiles, the
# foreign jobs are open, and that is a blocker the table must not paper over.
# Case 8 is the first part (M1): a job ID is matched exactly.
# ============================================================
section "Test 18: JES spool authorization"

JW_PW=JW90PASS
JW_GATE="${FTPD_GATE:-0}"
JW_OUT="$TMPDIR/jw_out.log"
JW_PROF="SYS1.SECURE.CNTL(PROFILES)"
JW_USRS="SYS1.SECURE.CNTL(USERS)"
JW_PROF_ORIG="$TMPDIR/jw_profiles.orig"
JW_USERS_ORIG="$TMPDIR/jw_users.orig"
JW_PROF_BASE="$TMPDIR/jw_profiles.base"
JW_ME=$(echo "$USER" | tr '[:lower:]' '[:upper:]')
JW_CUR=""           # fixture in effect
JW_RELOAD=batch     # batch (RAKFPROF procedure) or console (S RAKFPROF)
JW_DIRTY=0          # PROFILES or USERS differ from the original
JW_USERS_MADE=0
JW_SKIPPED=""
JW_FINDINGS=""
JW_NFIND=0
# user:default group:connected group:option
JW_TABLE="JWN0:USER:: AUDU:USER:AUDIT: OPSX:USER:OPS: ALTU:USER:ALT: RDOU:USER:RDOW:
OWNX:USER:: OWNX2:USER:: ABC:USER:: ABCD:USER:: ABCDEFG:USER::
MIKE1:USER:: MIKE2:USER:: MIKE10:USER:: ADMA:ADMIN:: ADMB:USER:ADMIN:
OPSU:USER::OPERATIONS NOBU:USER:NOBG:"
declare -A JID JOWN

jw_skip() {
    echo -e "  ${YELLOW}SKIPPED${NC}: $1"
    JW_SKIPPED="${JW_SKIPPED}\n  - $1"
}

# A proof or a precondition of the examples that does not hold: the server is
# not at fault, the plan is.  Counted apart from FAIL; a gate run fails on it.
jw_finding() {
    echo -e "  ${YELLOW}FINDING${NC}: $1"
    JW_NFIND=$((JW_NFIND + 1))
    JW_FINDINGS="${JW_FINDINGS}\n  - $1"
}

# --------------------------------------------------------------------
# One FTP session as USER.  CMDS is the command text; a line "--" splits
# the answers into segments jw_seg.N (segment 1 is the login).
# --------------------------------------------------------------------
JW_LOGFAIL=" "
jw_ftp() {
    local u="$1" cmds="$2" pw="$JW_PW" uu
    uu=$(echo "$u" | tr '[:lower:]' '[:upper:]')
    [ "$uu" = "$JW_ME" ] && pw="$PASS"
    cmds=$(printf '%s\n' "$cmds" | sed '/^$/d; s/^--$/quote NOOP/')
    printf 'user %s %s\nquote NOOP\n%s\nquit\n' "$u" "$pw" "$cmds" \
        | timeout 180 ftp -n -v -p -P "$PORT" "$HOST" > "$JW_OUT" 2>&1
    rm -f "$TMPDIR"/jw_seg.*
    awk -v p="$TMPDIR/jw_seg." 'BEGIN { n = 1 }
        /^200 OK/ { n++; next }
        { print > (p n) }' "$JW_OUT"
    JW_LOGIN=1
    if ! grep -q '^230' "$JW_OUT"; then
        JW_LOGIN=0
        case "$JW_LOGFAIL" in
        *" $uu "*) ;;
        *) JW_LOGFAIL="$JW_LOGFAIL$uu "
           fail "login as $u failed: $(grep '^[45][0-9][0-9]' "$JW_OUT" | head -1)" ;;
        esac
    fi
}
jw_seg() { grep -v '^221' "$TMPDIR/jw_seg.$1" 2>/dev/null; }

# --------------------------------------------------------------------
# Batch jobs as the test user.
# --------------------------------------------------------------------
# JCL file name, job id (JOBnnnnn) out: submit.
jw_submit_file() {
    ftp_run "site filetype=jes
type ascii
put $1 jw_batch.jcl
site filetype=seq" "$TMPDIR/jw_b.log"
    grep -o 'JOB[0-9][0-9][0-9][0-9][0-9]' "$TMPDIR/jw_b.log" | head -1
}

# Waits for job $1 to reach OUTPUT as the test user's own job; the spool goes
# to $2.  Returns 1 after about 3 minutes.
jw_batch_wait() {
    local id="$1" out="$2" n=0
    while [ $n -lt 90 ]; do
        ftp_run "site filetype=jes
ls
site filetype=seq" "$TMPDIR/jw_b.ls"
        if grep -q " $id .*OUTPUT" "$TMPDIR/jw_b.ls"; then
            ftp_run "site filetype=jes
get $id $out
site filetype=seq" "$TMPDIR/jw_b.log"
            [ -s "$out" ]
            return
        fi
        sleep 2
        n=$((n + 1))
    done
    return 1
}

# TSO commands on stdin (column 1!), run under IKJEFT01 as a RAKF administrator,
# then the RAKF batch procedures named in $2 (RAKFUSER, RAKFPROF).  The spool is
# left in $TMPDIR/jw_tso.out.
jw_tso() {
    local step="$1" id
    {
        echo "//JW90TSO  JOB (ACCT),'FTPD 90 SETUP',CLASS=A,MSGCLASS=X,GROUP=RAKFADM"
        echo "//TSO      EXEC PGM=IKJEFT01,REGION=4096K"
        echo "//SYSPRINT DD  SYSOUT=*"
        echo "//SYSTSPRT DD  SYSOUT=*"
        echo "//SYSTSIN  DD  *"
        cat
        echo "/*"
        for s in $step; do echo "//$s  EXEC $s"; done
    } > "$TMPDIR/jw_tso.jcl"
    id=$(jw_submit_file "$TMPDIR/jw_tso.jcl")
    [ -n "$id" ] && jw_batch_wait "$id" "$TMPDIR/jw_tso.out"
}

# Only the RAKF procedures, nothing on stdin.
jw_proc() {
    local id
    {
        echo "//JW90PRC  JOB (ACCT),'FTPD 90 SETUP',CLASS=A,MSGCLASS=X,GROUP=RAKFADM"
        for s in $1; do echo "//$s  EXEC $s"; done
    } > "$TMPDIR/jw_tso.jcl"
    id=$(jw_submit_file "$TMPDIR/jw_tso.jcl")
    [ -n "$id" ] && jw_batch_wait "$id" "$TMPDIR/jw_tso.out"
}

# --------------------------------------------------------------------
# Environment report: RAKF level, FTPD and libc370 version, image
# --------------------------------------------------------------------
# The level is read from the banner RAKF puts into ICHSEC00 ("RAKF V2R0M0"),
# not derived from a tag: dump the load module and look for the text in hex.
jw_rakf_level() {
    local jcl="$TMPDIR/jw_ver.jcl" id hex m
    cat > "$jcl" << 'JCLEOF'
//JW90VER  JOB (ACCT),'FTPD 90 RAKF',CLASS=A,MSGCLASS=X
//S1       EXEC PGM=AMASPZAP
//SYSPRINT DD SYSOUT=*
//SYSLIB   DD DSN=SYS1.LINKLIB,DISP=SHR
//SYSIN    DD *
 DUMPT ICHSEC00
/*
JCLEOF
    id=$(jw_submit_file "$jcl")
    [ -n "$id" ] || { echo "unknown"; return; }
    jw_batch_wait "$id" "$TMPDIR/jw_ver.out" || { echo "unknown"; return; }
    hex=$(grep -E '^ [0-9A-F]{6}  ' "$TMPDIR/jw_ver.out" \
          | sed 's/^ [0-9A-F]\{6\}  //; s/\*.*//' | tr -d ' \n')
    m=$(echo "$hex" | grep -o 'D9C1D2C640E5F.D9F.D4F.' | head -1)
    if [ -z "$m" ]; then echo "unknown"; return; fi
    echo "V${m:13:1}R${m:17:1}M${m:21:1}"
}

JW_RAKF=$(jw_rakf_level)
JW_FTPDV=""
JW_LIBC=""
if [ -n "$FTPD_CONSOLE" ]; then
    JW_VER=$($FTPD_CONSOLE "F FTPD,VERSION" 2>&1)
    JW_FTPDV=$(echo "$JW_VER" | sed -n 's/.*FTPD016I FTPD \([^ ]* ([^)]*)\).*/\1/p' | head -1)
    JW_LIBC=$(echo "$JW_VER" | sed -n 's/.*FTPD016I LIBC370 \([^ ]* ([^)]*)\).*/\1/p' | head -1)
fi
[ -n "$JW_LIBC" ] || JW_LIBC="${FTPD_LIBC370:-}"
info "RAKF:    ${JW_RAKF}"
info "FTPD:    ${JW_FTPDV:-not known (no FTPD_CONSOLE)}"
info "libc370: ${JW_LIBC:-not known (no FTPD_CONSOLE, no FTPD_LIBC370)}"
info "image:   ${FTPD_IMAGE:-not given (FTPD_IMAGE)}"
info "mode:    $([ "$JW_GATE" = 1 ] && echo gate || echo development)"

JW_READY=1
if [ "$JW_RAKF" = "V2R0M0" ]; then
    pass "RAKF level is V2R0M0 (ICHSEC00 banner)"
elif [ "$JW_GATE" = 1 ]; then
    fail "RAKF level is $JW_RAKF, the gate needs V2R0M0"
    JW_READY=0
else
    info "RAKF level $JW_RAKF is not V2R0M0: development run, not a gate"
fi
if [ "$JW_GATE" = 1 ]; then
    [ -n "$JW_LIBC" ]      || fail "gate: the libc370 version is not known"
    [ -n "$FTPD_IMAGE" ]   || fail "gate: FTPD_IMAGE (image and digest) is not given"
fi

# --------------------------------------------------------------------
# PROFILES fixtures: the lines of the examples A, B, D, E, F (plan 3.3)
# Each line is ENTITY|GROUP|ACCESS; an empty group is the universal line.
# --------------------------------------------------------------------
jw_fx_lines() {
    case "$1" in
    0)  ;;
    N)  printf '%s\n' '*||NONE' ;;
    3)  printf '%s\n' '*||NONE' 'OWNX.*||NONE' 'OWNX.*|RDOW|READ' ;;
    4)  printf '%s\n' '*||NONE' '*|AUDIT|READ' ;;
    A)  printf '%s\n' '*||NONE' '*|AUDIT|READ' 'SYSTEM.*||NONE' ;;
    B)  printf '%s\n' '*||READ' '*|OPS|UPDATE' 'SYSTEM.*||NONE' ;;
    5)  printf '%s\n' '*||NONE' '*|ALT|ALTER' '*|OPS|UPDATE' 'OPSX.*||NONE' \
                      'OPSX.*|OPS|READ' ;;
    6)  printf '%s\n' '*||NONE' 'ABC.*||NONE' 'ABC.*|AUDIT|READ' ;;
    12) printf '%s\n' '*||READ' '*|NOBG|NONE' ;;
    14) printf '%s\n' '*||NONE' 'ABCDEFG.PROD*||NONE' 'ABCDEFG.PROD*|AUDIT|READ' ;;
    D)  printf '%s\n' '*||NONE' '*|ADMIN|UPDATE' ;;
    E)  printf '%s\n' '*||READ' '*|ADMIN|UPDATE' 'SYSTEM.*||NONE' ;;
    E2) printf '%s\n' '*||READ' '*|ADMIN|UPDATE' 'SYSTEM.*||NONE' \
                      'SYSTEM.*|ADMIN|UPDATE' ;;
    F)  printf '%s\n' '*||NONE' '*|ADMIN|UPDATE' \
            'MIKE1.*||NONE' 'MIKE1.*|ADMIN|UPDATE' 'MIKE1.*|MIKE2|UPDATE' \
            'MIKE2.*||NONE' 'MIKE2.*|ADMIN|UPDATE' 'MIKE2.*|MIKE1|UPDATE' ;;
    Fv) printf '%s\n' '*||NONE' '*|ADMIN|UPDATE' \
            'MIKE1.*||NONE' 'MIKE1.*|MIKE2|UPDATE' \
            'MIKE2.*||NONE' 'MIKE2.*|MIKE1|UPDATE' ;;
    Bn) printf '%s\n' '*||READ' '*|OPS|UPDATE' ;;
    BROKEN) printf '%s\n' '*|ADMIN|UPDATE' ;;
    *)  return 1 ;;
    esac
}

# The PROFILES lines of a fixture (class cols 1-8, entity 9-52, group 53-60,
# access from 61).
jw_fx_render() {
    jw_fx_lines "$1" | awk -F'|' '{ printf "%-8s%-44s%-8s%s\n", "JESSPOOL", $1, $2, $3 }'
}

# The command sequence of a fixture, as in plan 3.3.
jw_fx_cmds() {
    jw_fx_lines "$1" | awk -F'|' '
        $2 == "" { printf "RDEF %s CL(JESSPOOL) UACC(%s)\n", $1, $3; next }
                 { printf "PE %s CL(JESSPOOL) ID(%s) ACCESS(%s)\n", $1, $2, $3 }'
}

jw_get() {   # dataset(member) local-file
    rm -f "$2"
    ftp_run "type ascii
get '$1' $2" "$TMPDIR/jw_get.log"
    [ -s "$2" ]
}

# The base: the saved PROFILES plus the FTPAUTH line the test users need.
jw_make_base() {
    awk '
        { print }
        substr($0, 9, 7) == "FTPAUTH" && substr($0, 53, 5) == "ADMIN" &&
        substr($0, 61, 4) == "READ" {
            printf "%-8s%-44s%-8s%s\n", "FACILITY", "FTPAUTH", "USER", "READ"
        }' "$JW_PROF_ORIG" > "$JW_PROF_BASE"
    grep -q '^FACILITYFTPAUTH  *USER  *READ' "$JW_PROF_BASE"
}

# PROFILES = base + the JESSPOOL block of fixture $1, inserted at its sorted
# place (JESSPOOL sorts between FACILITY and TAPEVOL).
jw_build_profiles() {
    jw_fx_render "$1" > "$TMPDIR/jw_block.txt"
    awk -v blk="$TMPDIR/jw_block.txt" '
        BEGIN { while ((getline l < blk) > 0) b[++n] = l }
        !done && $0 !~ /^[*#]/ && substr($0, 1, 8) > "JESSPOOL" {
            for (i = 1; i <= n; i++) print b[i]
            done = 1
        }
        { print }
        END { if (!done) for (i = 1; i <= n; i++) print b[i] }' "$JW_PROF_BASE"
}

# Every PUT of a member appends to the PDS and leaves the old copy behind;
# compress SYS1.SECURE.CNTL before it runs full (SE37).
JW_PUTS=0
jw_compress() {
    local id
    {
        echo "//JW90CMP  JOB (ACCT),'FTPD 90 COMPRESS',CLASS=A,MSGCLASS=X"
        echo "//C        EXEC PGM=IEBCOPY"
        echo "//SYSPRINT DD SYSOUT=*"
        echo "//SYSUT3   DD UNIT=SYSDA,SPACE=(TRK,(5,5))"
        echo "//SYSUT4   DD UNIT=SYSDA,SPACE=(TRK,(5,5))"
        echo "//IN       DD DSN=SYS1.SECURE.CNTL,DISP=OLD"
        echo "//SYSIN    DD *"
        echo " COPY INDD=IN,OUTDD=IN"
        echo "/*"
    } > "$TMPDIR/jw_cmp.jcl"
    id=$(jw_submit_file "$TMPDIR/jw_cmp.jcl")
    [ -n "$id" ] && jw_batch_wait "$id" "$TMPDIR/jw_cmp.out" \
        && grep -q 'IEB147I' "$TMPDIR/jw_cmp.out"
}

jw_put() {   # local-file dataset(member)
    if [ $((JW_PUTS % 12)) = 0 ]; then
        jw_compress || info "SYS1.SECURE.CNTL could not be compressed"
    fi
    JW_PUTS=$((JW_PUTS + 1))
    ftp_run "type ascii
put $1 '$2'" "$TMPDIR/jw_put.log"
    if grep -q '^552 .*SE37' "$TMPDIR/jw_put.log"; then
        info "SYS1.SECURE.CNTL is full (SE37): compress it and run again"
    fi
    grep -q '^250 ' "$TMPDIR/jw_put.log"
}

# Install fixture $1 and reload.  "bad" as $2: the reload is expected to abend.
# Returns 0 if the reload ended as expected.
jw_fx() {
    local fx="$1" mode="${2:-good}"
    [ "$fx" = "$JW_CUR" ] && [ "$mode" = good ] && return 0
    jw_build_profiles "$fx" > "$TMPDIR/jw_new.txt"
    JW_DIRTY=1
    if ! jw_put "$TMPDIR/jw_new.txt" "$JW_PROF"; then
        fail "fixture $fx: PROFILES could not be written"
        return 1
    fi
    if [ "$JW_RELOAD" = console ]; then
        $FTPD_CONSOLE "S RAKFPROF" > "$TMPDIR/jw_tso.out" 2>&1
    else
        jw_proc RAKFPROF
    fi
    if [ "$mode" = bad ]; then
        grep -q 'RAKFPROF4  PERMIT RECORD WITHOUT UNIVERSAL ACCESS RECORD' "$TMPDIR/jw_tso.out"
        return
    fi
    if grep -q 'RAKFPROF7  RAKF PROFILES UPDATED' "$TMPDIR/jw_tso.out"; then
        JW_CUR="$fx"
        return 0
    fi
    fail "fixture $fx: the reload (RAKFPROF) did not report RAKFPROF7"
    grep 'RAKFPROF' "$TMPDIR/jw_tso.out" | head -5 | sed 's/^/    /'
    return 1
}

# Put the files back and delete the test users.  Safe to call twice.
jw_restore() {
    [ "$JW_DIRTY" = 1 ] || return 0
    JW_DIRTY=0
    local u cmds="" ok=1
    jw_put "$JW_PROF_ORIG" "$JW_PROF" || ok=0
    if [ "$JW_USERS_MADE" = 1 ]; then
        for u in $(jw_users); do cmds="${cmds}DELUSER $u\n"; done
        printf '%b' "$cmds" | jw_tso "RAKFUSER RAKFPROF" || ok=0
    else
        jw_proc RAKFPROF || ok=0
    fi
    jw_get "$JW_PROF" "$TMPDIR/jw_profiles.now" || ok=0
    jw_get "$JW_USRS" "$TMPDIR/jw_users.now" || ok=0
    if [ $ok = 1 ] && cmp -s "$JW_PROF_ORIG" "$TMPDIR/jw_profiles.now" \
       && cmp -s "$JW_USERS_ORIG" "$TMPDIR/jw_users.now"; then
        pass "the fixtures are removed: PROFILES and USERS are as they were"
    else
        fail "the fixtures could not be removed: restore SYS1.SECURE.CNTL(PROFILES) and (USERS) by hand"
        diff "$JW_PROF_ORIG" "$TMPDIR/jw_profiles.now" | head -5 | sed 's/^/    /'
        diff "$JW_USERS_ORIG" "$TMPDIR/jw_users.now" | head -5 | sed 's/^/    /'
    fi
    JW_CUR=""
}
cleanup() { jw_restore; rm -rf "$TMPDIR"; }

jw_users() { echo $JW_TABLE | tr ' ' '\n' | cut -d: -f1; }

# --------------------------------------------------------------------
# Jobs and what a session sees
# --------------------------------------------------------------------
# $1 owner, $2 jobname: submit a job of that owner and wait for OUTPUT.
# The job ID is left in JW_NEWID (empty on failure).
jw_newjob() {
    local owner="$1" name="$2" n=0
    JW_NEWID=""
    {
        echo "//$name JOB (ACCT),'FTPD 90',CLASS=A,MSGCLASS=X"
        echo "//S1 EXEC PGM=IEFBR14"
    } > "$TMPDIR/jw_new.jcl"
    jw_ftp "$owner" "site filetype=jes
type ascii
put $TMPDIR/jw_new.jcl jw_new.jcl"
    JW_NEWID=$(grep -o 'JOB[0-9][0-9][0-9][0-9][0-9]' "$JW_OUT" | head -1)
    [ -n "$JW_NEWID" ] || return 1
    while [ $n -lt 45 ]; do
        jw_ftp "$owner" "site filetype=jes
--
ls"
        jw_seg 3 | grep -q "$JW_NEWID .*OUTPUT" && return 0
        sleep 2
        n=$((n + 1))
    done
    return 1
}

# Is job $2 in the default listing of $1?
jw_present() {
    jw_ftp "$1" "site filetype=jes
--
ls"
    jw_seg 3 | grep -qw "$2"
}

# One probe of job $2 by user $1, with the owner filter $3 (may be empty).
# Sets JT_OWN (reply code of SITE JESOWNER), JT_L (listed), JT_R (RETR gave
# data), JT_J (LIST of the job gave data) and JT_CLASS: ALLOW, HIDE or MIXED.
jw_try() {
    local u="$1" id="$2" f="$3" own=""
    [ -n "$f" ] && own="site jesowner=$f"
    rm -f "$TMPDIR/jw_got"
    jw_ftp "$u" "site filetype=jes
type ascii
--
$own
--
ls
--
get $id $TMPDIR/jw_got
--
ls $id -"
    JT_OWN=""
    if [ "$JW_LOGIN" = 0 ]; then JT_CLASS=NOLOGIN; JT_L=0; JT_R=0; JT_J=0; return; fi
    [ -n "$f" ] && JT_OWN=$(jw_seg 3 | grep -o '^[0-9][0-9][0-9]' | tail -1)
    JT_L=0; JT_R=0; JT_J=0
    jw_seg 4 | grep -qw "$id" && JT_L=1
    if [ -s "$TMPDIR/jw_got" ] && ! jw_seg 5 | grep -q '^550'; then JT_R=1; fi
    # Only a JOBnnnnn id lists the spool files of a job; any other id (STC,
    # TSU) gives the ordinary job listing, so the file listing answers nothing.
    case "$id" in
    JOB*) if jw_seg 6 | grep -q '^250' && ! jw_seg 6 | grep -q '^550'; then JT_J=1; fi ;;
    *)    JT_J=$JT_L ;;
    esac
    if [ $JT_L$JT_R$JT_J = 111 ]; then JT_CLASS=ALLOW
    elif [ $JT_L$JT_R$JT_J = 000 ]; then JT_CLASS=HIDE
    else JT_CLASS=MIXED; fi
}

# jw_expect USER ID FILTER EXPECT LABEL
jw_expect() {
    jw_try "$1" "$2" "$3"
    if [ "$JT_CLASS" = "$4" ]; then
        pass "$5"
    else
        fail "$5: expected $4, got $JT_CLASS (listed=$JT_L retr=$JT_R list-job=$JT_J jesowner=${JT_OWN:--})"
    fi
}

# jw_m USER FILTER LABEL KEY:EXPECT ... : one probe per job KEY of JID.
jw_m() {
    local u="$1" f="$2" label="$3" pair key exp
    shift 3
    for pair in "$@"; do
        key="${pair%%:*}"; exp="${pair##*:}"
        jw_expect "$u" "${JID[$key]}" "$f" "$exp" "$label: $key -> $exp"
    done
}

# SITE JESOWNER=$2 as user $1: reply code in JW_OC, text in JW_OT.
jw_owner() {
    jw_ftp "$1" "site filetype=jes
--
site jesowner=$2"
    JW_OT=$(jw_seg 3 | grep '^[0-9][0-9][0-9]' | tail -1 | tr -d '\r')
    JW_OC="${JW_OT%% *}"
    [ "$JW_LOGIN" = 0 ] && JW_OC=NOLOGIN
}

# jw_owner_is USER VALUE CODE LABEL
jw_owner_is() {
    jw_owner "$1" "$2"
    if [ "$JW_OC" = "$3" ]; then pass "$4"; else fail "$4: expected $3, got '$JW_OT'"; fi
}

# DELE of job $2 by $1: OK, NOCANCEL, HIDE or OTHER in JW_DEL.
jw_del() {
    jw_ftp "$1" "site filetype=jes
--
delete $2"
    local r
    r=$(jw_seg 3 | grep '^[0-9][0-9][0-9]' | tail -1)
    [ "$JW_LOGIN" = 0 ] && r="NOLOGIN"
    case "$r" in
    250\ Cancel\ successful*)        JW_DEL=OK ;;
    550\ Access\ denied*cannot\ cancel*) JW_DEL=NOCANCEL ;;
    550\ Jobid*not\ found*)          JW_DEL=HIDE ;;
    *)                               JW_DEL=OTHER ;;
    esac
}

# jw_dele_case ACTOR OWNER EXPECT LABEL: a fresh job of OWNER, deleted by ACTOR.
JW_DCNT=0
jw_dele_case() {
    local actor="$1" owner="$2" exp="$3" label="$4" id gone=1 want
    JW_DCNT=$((JW_DCNT + 1))
    if ! jw_newjob "$owner" "JWD$JW_DCNT"; then
        fail "$label: could not make a job for $owner"
        return
    fi
    id="$JW_NEWID"
    jw_del "$actor" "$id"
    jw_present "$owner" "$id" && gone=0
    want=0; [ "$exp" = OK ] && want=1
    if [ "$JW_DEL" = "$exp" ] && [ $gone = $want ]; then
        pass "$label: DELE $exp, job $([ $gone = 1 ] && echo gone || echo kept)"
    else
        fail "$label: expected $exp, got $JW_DEL (job $([ $gone = 1 ] && echo gone || echo kept))"
    fi
    [ $gone = 0 ] && jw_del "$owner" "$id"
    return 0
}

# Job IDs the default listing (or the owner filter $2) of $1 shows.
jw_ids() {
    local own=""
    [ -n "$2" ] && own="site jesowner=$2"
    jw_ftp "$1" "site filetype=jes
--
$own
--
ls"
    jw_seg 4 | awk '$2 ~ /^(JOB|STC|TSU)[0-9][0-9][0-9][0-9][0-9]$/ { print $2 }'
}

# Does the default listing of $1 show only jobs of $1 (and at least one)?
jw_only_own() {
    jw_ftp "$1" "site filetype=jes
--
ls"
    jw_seg 3 | awk -v u="$1" '
        $2 ~ /^(JOB|STC|TSU)[0-9][0-9][0-9][0-9][0-9]$/ { n++; if ($3 != u) bad++ }
        END { exit !(n > 0 && bad == 0) }'
}

# Byte-exact reply lines of one command in a session, job IDs normalised.
jw_reply() {   # user "setup" "command"
    jw_ftp "$1" "site filetype=jes
type ascii
$2
--
$3"
    jw_seg 3 | grep '^[0-9][0-9][0-9]' | sed 's/\(JOB\|STC\|TSU\)[0-9][0-9][0-9][0-9][0-9]/XXXnnnnn/g'
}

# One job per user in JID; with "missing" only those that are gone (a
# server without the policy lets the wildcard DELE of Fall 8 remove them all).
jw_mkjobs() {
    local u
    jw_mk IBMUSER "$JW_ME" JWIBM90 "$1"
    for u in $(jw_users); do
        if [ "$u" = ABCDEFG ]; then
            jw_mk PRODX "$u" PRODX "$1"
            jw_mk TESTX "$u" TESTX "$1"
        else
            jw_mk "$u" "$u" "J$u" "$1"
        fi
    done
}
jw_mk() {   # key owner jobname [missing]
    if [ "$4" = missing ] && [ -n "${JID[$1]}" ] && jw_present "$2" "${JID[$1]}"; then
        return 0
    fi
    jw_newjob "$2" "$3" && JID[$1]="$JW_NEWID"
}

# --------------------------------------------------------------------
# Setup: the saved originals, the test users, one job each
# --------------------------------------------------------------------
JW_SETUP=0
if [ "$JW_READY" = 1 ]; then
    if jw_get "$JW_PROF" "$JW_PROF_ORIG" && jw_get "$JW_USRS" "$JW_USERS_ORIG"; then
        JW_SETUP=1
    else
        fail "setup: PROFILES and USERS of the RAKF administrator cannot be read ($JW_PROF)"
    fi
fi
if [ "$JW_SETUP" = 1 ] && grep -q '^JESSPOOL' "$JW_PROF_ORIG"; then
    fail "setup: PROFILES already has JESSPOOL lines; the test needs a clean container"
    JW_SETUP=0
fi
if [ "$JW_SETUP" = 1 ]; then
    for u in $(jw_users); do
        if grep -q "^$u " "$JW_USERS_ORIG"; then
            fail "setup: user $u exists already; the test needs a clean container"
            JW_SETUP=0
        fi
    done
fi
if [ "$JW_SETUP" = 1 ] && ! jw_make_base; then
    fail "setup: PROFILES has no FACILITY FTPAUTH line for ADMIN to extend"
    JW_SETUP=0
fi

if [ "$JW_SETUP" = 1 ]; then
    JW_DIRTY=1
    JW_USERS_MADE=1
    jw_fx 0 || JW_SETUP=0
fi
if [ "$JW_SETUP" = 1 ]; then
    for e in $JW_TABLE; do
        IFS=: read -r u d c o <<< "$e"
        echo "ADDUSER $u PASSWORD($JW_PW) DFLTGRP($d)${c:+ GROUP($c)}${o:+ $o}"
    done | jw_tso RAKFUSER
    if ! grep -q 'RAKFUIDS4  USER TABLE UPDATED' "$TMPDIR/jw_tso.out"; then
        fail "setup: the users could not be added (RAKFUIDS4 not reported)"
        grep -i 'ADDUSER\|RAKFUIDS' "$TMPDIR/jw_tso.out" | head -8 | sed 's/^/    /'
        JW_SETUP=0
    fi
fi
if [ "$JW_SETUP" = 1 ]; then
    for u in $(jw_users); do
        jw_ftp "$u" "pwd"
        [ "$JW_LOGIN" = 1 ] || JW_SETUP=0
    done
    [ "$JW_SETUP" = 1 ] && pass "setup: the $(jw_users | wc -l | tr -d ' ') test users exist and log in"
fi

JW_STC_OUT=0
if [ "$JW_SETUP" = 1 ]; then
    JW_NOOWN_NAME="JWO$(( (RANDOM % 90000) + 10000 ))"
    if [ -n "$FTPD_RDR" ]; then
        (   exec 7<>"/dev/tcp/${FTPD_RDR%:*}/${FTPD_RDR#*:}" &&
            printf '//%s JOB (ACCT),NOOWNER,CLASS=A,MSGCLASS=X\n//S1 EXEC PGM=IEFBR14\n' \
                "$JW_NOOWN_NAME" >&7 ) 2>/dev/null
    fi
    jw_mkjobs
    for u in IBMUSER $(jw_users | grep -vx ABCDEFG) PRODX TESTX; do
        [ -n "${JID[$u]}" ] || { fail "setup: no job for $u"; JW_SETUP=0; }
    done

    # The job IDs of the STC and of the job without owner: found by an
    # administrator under fixture D, which shows everything.
    jw_fx D || JW_SETUP=0
    jw_ids ADMA '*' > "$TMPDIR/jw_all.ids"
    jw_seg 4 > "$TMPDIR/jw_all.rows"
    JID[STC]=$(awk '$2 ~ /^STC[0-9]+$/ && $3 == "SYSTEM" && /OUTPUT/ { print $2; exit }' "$TMPDIR/jw_all.rows")
    if [ -n "${JID[STC]}" ]; then
        JW_STC_OUT=1
    else
        JID[STC]=$(awk '$2 ~ /^STC[0-9]+$/ && $3 == "SYSTEM" { print $2; exit }' "$TMPDIR/jw_all.rows")
    fi
    JID[NOOWN]=$(awk -v n="$JW_NOOWN_NAME" '$1 == n { print $2; exit }' "$TMPDIR/jw_all.rows")
    info "jobs: STC ${JID[STC]:-none} (finished: $JW_STC_OUT), no owner ${JID[NOOWN]:-none}"
    [ -n "${JID[STC]}" ] || jw_skip "an STC owned by SYSTEM was not found: the STC checks"
    [ -n "${JID[NOOWN]}" ] || jw_skip "FTPD_RDR is not set or the reader job did not appear: the checks of the job without owner"
fi
[ "$JW_SETUP" = 1 ] || { [ "$JW_READY" = 1 ] && fail "Test 18: the setup failed, the cases did not run"; }

# jw_m with keys that exist.
jw_have() {   # key...: true if all have an id
    local k
    for k in "$@"; do [ -n "${JID[$k]}" ] || return 1; done
}

# --------------------------------------------------------------------
# Proof of the examples: the command sequence of 3.3 writes the 3.3 lines
# --------------------------------------------------------------------
jw_proof() {
    local ex="$1" got exp
    exp=$(jw_fx_render "$ex")
    jw_fx 0 || return
    jw_fx_cmds "$ex" | jw_tso RAKFPROF
    JW_CUR=""
    cp "$TMPDIR/jw_tso.out" "$TMPDIR/jw_proof.out"
    jw_get "$JW_PROF" "$TMPDIR/jw_proof.prof"
    got=$(grep '^JESSPOOL' "$TMPDIR/jw_proof.prof")
    if [ "$got" = "$exp" ]; then
        pass "Proof $ex: the command sequence writes the lines of the example"
        return
    fi
    jw_finding "Proof $ex: the command sequence of plan 3.3 does not write the lines of the example"
    grep 'RDEFINE:\|PERMIT:\|RAKFPROF[0-9]' "$TMPDIR/jw_proof.out" | sort | uniq -c | head -6 | sed 's/^/    /'
    echo "    expected:"; echo "$exp" | sed 's/^/      /'
    echo "    written:";  echo "$got" | sed 's/^/      /'
}

if [ "$JW_SETUP" = 1 ]; then
    for ex in A B D E F; do jw_proof "$ex"; done
fi

# Runs a block only if the setup worked.
if [ "$JW_SETUP" = 1 ]; then

# ============================================================
# Fall 1: empty JESSPOOL class (regression M3, release blocker)
# ============================================================
jw_fx 0
jw_expect "$JW_ME" "${JID[IBMUSER]}" "" ALLOW "Fall 1: the own job is read and listed"
for k in JWN0 STC NOOWN; do
    jw_have "$k" && jw_expect "$JW_ME" "${JID[$k]}" "" HIDE "Fall 1: the job $k of someone else is hidden"
done
jw_only_own "$JW_ME" && pass "Fall 1: LIST shows only the own jobs" \
                     || fail "Fall 1: LIST shows jobs of others"
jw_owner_is "$JW_ME" '*' 501 "Fall 1: SITE JESOWNER=* is refused"
JW1_TEXT="$JW_OT"
jw_owner_is "$JW_ME" "$JW_ME" 200 "Fall 1: SITE JESOWNER=<own> is accepted"
jw_dele_case "$JW_ME" JWN0 HIDE "Fall 1: DELE of a foreign job"
jw_dele_case "$JW_ME" "$JW_ME" OK "Fall 1: DELE of an own job"
info "Fall 1 report: RAKF $JW_RAKF, libc370 ${JW_LIBC:-unknown}, image ${FTPD_IMAGE:-unknown}"

# ============================================================
# Fall 8 (M1): a job ID is matched exactly
# ============================================================
JW_JCL="$TMPDIR/jw_job.jcl"
cat > "$JW_JCL" << 'JCLEOF'
//FTPD90A  JOB (ACCT),'FTPD 90',CLASS=A,MSGCLASS=X
//STEP1    EXEC PGM=IEFBR14
//SYSPRINT DD SYSOUT=*
JCLEOF

# Submit the JCL once; print the job ID JES assigned (empty on failure).
jw_submit() {
    ftp_run "site filetype=jes
type ascii
put $JW_JCL jw_job.jcl
site filetype=seq" "$TMPDIR/jw_submit.log"
    grep -o '[J][O][B][0-9][0-9][0-9][0-9][0-9]' "$TMPDIR/jw_submit.log" | head -1
}

# Wait until the job shows as OUTPUT in the JES listing (about 60 s).
jw_wait_output() {
    local n=0
    while [ $n -lt 30 ]; do
        ftp_run "site filetype=jes
ls
site filetype=seq" "$TMPDIR/jw_ls.log"
        if grep -q "^[A-Z0-9#@\$]* *$1 .*OUTPUT" "$TMPDIR/jw_ls.log"; then
            return 0
        fi
        sleep 2
        n=$((n + 1))
    done
    return 1
}

# The job IDs of the user's jobs, one per line, sorted.
jw_jobs() {
    ftp_run "site filetype=jes
ls
site filetype=seq" "$TMPDIR/jw_all.log"
    grep -o '^[A-Z0-9#@$]* *JOB[0-9]\{5\}' "$TMPDIR/jw_all.log" \
        | awk '{ print $NF }' | sort
}

JW_ID1=$(jw_submit)
JW_ID2=$(jw_submit)
if [ -n "$JW_ID1" ] && [ -n "$JW_ID2" ] && [ "$JW_ID1" != "$JW_ID2" ] \
   && jw_wait_output "$JW_ID1" && jw_wait_output "$JW_ID2"; then
    pass "Fall 8: two jobs of the user are in OUTPUT ($JW_ID1, $JW_ID2)"
    JW8_OK=1
else
    fail "Fall 8: could not set up two finished jobs (got '$JW_ID1' '$JW_ID2')"
    JW8_OK=0
fi

if [ "$JW8_OK" = 1 ]; then
    JW_PREFIX="${JW_ID1%???}"          # JOBnn of JOBnnnnn
    JW_SHORT="JOB$((10#${JW_ID1#JOB}))"                      # without the zeros
    JW_LOWER=$(echo "${JW_PREFIX}*" | tr '[:upper:]' '[:lower:]')
    JW_BEFORE=$(jw_jobs)

    # A request that is not the exact job ID of one job: 501 (invalid job
    # ID) or 550, and nothing is opened, listed or removed.
    jw_refused() {
        local verb="$1" arg="$2" cmd out="$TMPDIR/jw_case.log"
        case "$verb" in
            delete) cmd="delete $arg" ;;
            get)    cmd="get $arg $TMPDIR/jw_got.txt" ;;
            ls)     cmd="ls $arg -" ;;
        esac
        rm -f "$TMPDIR/jw_got.txt"
        ftp_run "site filetype=jes
type ascii
$cmd
site filetype=seq" "$out"
        if grep -q '^\(501\|550\) ' "$out" \
           && ! grep -q '^\(125\|150\|250 Cancel\)' "$out" \
           && { [ ! -s "$TMPDIR/jw_got.txt" ]; }; then
            pass "Fall 8: $verb $arg refused (501/550)"
        else
            fail "Fall 8: $verb $arg was not refused"
            tail -4 "$out" | sed 's/^/    /'
        fi
    }

    for JW_ARG in 'JOB*' 'job*' "$JW_LOWER" "${JW_PREFIX}*" "$JW_SHORT" 'JOB1' '*'; do
        for JW_VERB in get ls delete; do
            # "ls *" is the job listing, not a spool file listing.
            [ "$JW_VERB$JW_ARG" = 'ls*' ] && continue
            jw_refused "$JW_VERB" "$JW_ARG"
        done
    done

    JW_AFTER=$(jw_jobs)
    if [ -n "$JW_BEFORE" ] && [ "$JW_BEFORE" = "$JW_AFTER" ]; then
        pass "Fall 8: no job is gone after the refused DELE requests"
    else
        fail "Fall 8: the user's jobs changed"
        echo "before:"; echo "$JW_BEFORE" | sed 's/^/    /'
        echo "after:";  echo "$JW_AFTER"  | sed 's/^/    /'
    fi

    # The exact ID still works, in lowercase as well.
    JW_LOW1=$(echo "$JW_ID1" | tr '[:upper:]' '[:lower:]')
    rm -f "$TMPDIR/jw_got.txt"
    ftp_run "site filetype=jes
type ascii
get $JW_LOW1 $TMPDIR/jw_got.txt
site filetype=seq" "$TMPDIR/jw_case.log"
    if [ -s "$TMPDIR/jw_got.txt" ]; then
        pass "Fall 8: the exact ID ($JW_LOW1, lowercase) is still retrieved"
    else
        fail "Fall 8: the exact ID is not retrieved"
        tail -4 "$TMPDIR/jw_case.log" | sed 's/^/    /'
    fi

    ftp_run "site filetype=jes
delete $JW_ID1
delete $JW_ID2
site filetype=seq" "$TMPDIR/jw_case.log"
    if [ "$(jw_jobs | grep -c "^\($JW_ID1\|$JW_ID2\)$")" = 0 ]; then
        pass "Fall 8: DELE with the exact ID removes the job"
    else
        fail "Fall 8: DELE with the exact ID left a job"
        tail -4 "$TMPDIR/jw_case.log" | sed 's/^/    /'
    fi
fi


jw_mkjobs missing

# ============================================================
# Fall 2: "*" NONE, a user without a permit (variant 3)
# ============================================================
jw_fx N
jw_expect JWN0 "${JID[JWN0]}" "" ALLOW "Fall 2: the own job is read and listed"
for k in OWNX IBMUSER STC NOOWN; do
    jw_have "$k" || continue
    jw_expect JWN0 "${JID[$k]}" "" HIDE "Fall 2: the job $k of someone else is hidden"
done
jw_have NOOWN && jw_expect JWN0 "${JID[NOOWN]}" "" HIDE "Fall 7: the job without owner is hidden without READ on *"
jw_only_own JWN0 && pass "Fall 2: LIST shows only the own jobs" || fail "Fall 2: LIST shows jobs of others"
jw_dele_case JWN0 OWNX HIDE "Fall 2: DELE of a foreign job"
jw_dele_case JWN0 JWN0 OK "Fall 2: DELE of an own job"

# ============================================================
# Fall 9: NLST, abbreviations, invalid owners, the same 501
# ============================================================
jw_ftp AUDU "site filetype=jes
--
nlist"
if jw_seg 3 | grep -qw "${JID[JWN0]}"; then fail "Fall 9: NLST lists a job of someone else"
else pass "Fall 9: NLST shows no job of someone else"; fi
jw_owner_is JWN0 '*' 501 "Fall 9: SITE JESOWNER=* without READ is refused"
JW9_TEXT="$JW_OT"
if [ "$JW9_TEXT" = "$JW1_TEXT" ]; then
    pass "Fall 9: the 501 text is the same for 'no READ' and 'no profile'"
else
    fail "Fall 9: the 501 text differs ('$JW9_TEXT' / '$JW1_TEXT')"
fi
jw_owner_is JWN0 '*' 501 "Fall 9: JESOWNER=* repeated"
jw_ftp JWN0 "site filetype=jes
--
site jeso=*"
JW_OT=$(jw_seg 3 | grep '^[0-9][0-9][0-9]' | tail -1)
case "$JW_OT" in 501*) pass "Fall 9: SITE JESO=* (abbreviation) is refused" ;;
                 *)    fail "Fall 9: SITE JESO=* gave '$JW_OT'" ;; esac
for v in ABCDEFGHI A.B 'AB%' 'ABC*'; do
    jw_owner_is JWN0 "$v" 501 "Fall 9: JESOWNER=$v is refused, not cut to 8"
done
jw_owner_is JWN0 '' 200 "Fall 9: an empty JESOWNER resets"

# ============================================================
# Fall 10: login in lower case
# ============================================================
JW_ME_L=$(echo "$JW_ME" | tr '[:upper:]' '[:lower:]')
jw_expect "$JW_ME_L" "${JID[IBMUSER]}" "" ALLOW "Fall 10: after a lower-case login the own job is visible"

# ============================================================
# Fall 15: OWNX against OWNX2, no profile
# ============================================================
jw_fx 0
jw_expect OWNX  "${JID[OWNX]}"  "" ALLOW "Fall 15: OWNX sees its own job"
jw_expect OWNX2 "${JID[OWNX2]}" "" ALLOW "Fall 15: OWNX2 sees its own job"
jw_expect OWNX  "${JID[OWNX2]}" "" HIDE  "Fall 15: OWNX does not see the job of OWNX2"
jw_expect OWNX2 "${JID[OWNX]}"  "" HIDE  "Fall 15: OWNX2 does not see the job of OWNX"
jw_dele_case OWNX OWNX2 HIDE "Fall 15: DELE of the job of OWNX2 by OWNX"

# ============================================================
# Fall 16: the 550 is the same, byte for byte
# ============================================================
jw_fx N
SETUP0="type ascii"
SETUP1="type ascii
site jesstatus=ACTIVE"
HID="${JID[OWNX]}"; MINE="${JID[JWN0]}"; NONE=JOB99999
for c in "get %s $TMPDIR/jw_got16" "delete %s" "ls %s -"; do
    cmd0=$(printf "$c" "$HID");  cmdN=$(printf "$c" "$NONE"); cmdM=$(printf "$c" "$MINE")
    word=${c%% *}
    jw_reply JWN0 "$SETUP0" "$cmd0" > "$TMPDIR/jw_16_h0"
    jw_reply JWN0 "$SETUP0" "$cmdN" > "$TMPDIR/jw_16_n0"
    jw_reply JWN0 "$SETUP1" "$cmd0" > "$TMPDIR/jw_16_h1"
    jw_reply JWN0 "$SETUP1" "$cmdM" > "$TMPDIR/jw_16_m1"
    jw_reply JWN0 "$SETUP1" "$cmdN" > "$TMPDIR/jw_16_n1"
    if grep -q '^550 Jobid XXXnnnnn not found' "$TMPDIR/jw_16_h0" \
       && cmp -s "$TMPDIR/jw_16_h0" "$TMPDIR/jw_16_n0"; then
        pass "Fall 16: $word hidden job and missing job give the same bytes"
    else
        fail "Fall 16: $word hidden job and missing job differ"
        sed 's/^/    hidden:  /' "$TMPDIR/jw_16_h0"; sed 's/^/    missing: /' "$TMPDIR/jw_16_n0"
    fi
    if cmp -s "$TMPDIR/jw_16_h1" "$TMPDIR/jw_16_m1" && cmp -s "$TMPDIR/jw_16_h1" "$TMPDIR/jw_16_n1" \
       && grep -q '^550 Jobid XXXnnnnn not found' "$TMPDIR/jw_16_h1"; then
        pass "Fall 16: $word hidden job, other status and missing job give the same bytes"
    else
        fail "Fall 16: $word hidden job, other status and missing job differ"
        sed 's/^/    hidden:  /' "$TMPDIR/jw_16_h1"; sed 's/^/    status:  /' "$TMPDIR/jw_16_m1"
        sed 's/^/    missing: /' "$TMPDIR/jw_16_n1"
    fi
done
jw_present JWN0 "${JID[JWN0]}" && jw_present OWNX "${JID[OWNX]}" \
    && pass "Fall 16: no job is gone after the refused requests" \
    || fail "Fall 16: a job is gone after the refused requests"

# ============================================================
# Fall 3: a profile OWNX.* with READ
# ============================================================
jw_fx 3
jw_owner_is RDOU OWNX 200 "Fall 3: JESOWNER=OWNX is accepted with READ"
jw_expect RDOU "${JID[OWNX]}" OWNX ALLOW "Fall 3: LIST and RETR of the job of OWNX"
jw_owner_is RDOU '*' 501 "Fall 3: JESOWNER=* is refused (no READ on the universal profile)"
jw_expect RDOU "${JID[JWN0]}" JWN0 HIDE "Fall 3: the job of JWN0 stays hidden"
jw_dele_case RDOU OWNX NOCANCEL "Fall 3: DELE with READ only"
jw_expect JWN0 "${JID[OWNX]}" OWNX HIDE "Fall 3: a user outside RDOW does not see the job of OWNX"

# ============================================================
# Fall 4: "*" READ for the auditor
# ============================================================
jw_fx 4
jw_owner_is AUDU '*' 200 "Fall 4: JESOWNER=* is accepted with READ on the universal profile"
jw_m AUDU '*' "Fall 4" JWN0:ALLOW OWNX:ALLOW IBMUSER:ALLOW
jw_have STC && jw_expect AUDU "${JID[STC]}" '*' ALLOW "Fall 4: the STC job of SYSTEM is visible without SYSTEM.*"
jw_have NOOWN && jw_expect AUDU "${JID[NOOWN]}" '*' ALLOW "Fall 7: the job without owner is visible with READ on *"
jw_dele_case AUDU OWNX NOCANCEL "Fall 4: DELE with READ only"
jw_ftp AUDU "site filetype=jes
--
site jesowner=*
--
nlist"
jw_seg 4 | grep -qw "${JID[JWN0]}" && pass "Fall 9: NLST shows what LIST shows" \
                                   || fail "Fall 9: NLST does not show the job of JWN0"

# ============================================================
# Fall 5: UPDATE and ALTER
# ============================================================
jw_fx 5
jw_dele_case OPSX OWNX OK "Fall 5: UPDATE deletes a foreign job"
jw_dele_case ALTU OWNX OK "Fall 5: ALTER deletes a foreign job"
jw_dele_case OPSX OPSX OK "Fall 5: the own job is deleted despite OPSX.* READ"
jw_dele_case JWN0 OWNX HIDE "Fall 5: a user with no permit does not delete a foreign job"

# ============================================================
# Fall 6: ABC.* against the owner ABCD
# ============================================================
jw_fx 6
jw_expect AUDU "${JID[ABC]}" ABC ALLOW "Fall 6: ABC.* with READ shows the job of ABC"
jw_expect AUDU "${JID[ABCD]}" ABCD HIDE "Fall 6: ABC.* does not show the job of ABCD"
jw_owner_is AUDU ABCD 501 "Fall 6: JESOWNER=ABCD is refused"
jw_expect ABC "${JID[ABC]}" "" ALLOW "Fall 6: ABC sees its own job"
jw_dele_case ABC ABC OK "Fall 6: ABC deletes its own job"
jw_dele_case AUDU ABCD HIDE "Fall 6: DELE of the job of ABCD"

# ============================================================
# Fall 12: NONE for a group
# ============================================================
jw_fx 12
jw_expect NOBU "${JID[NOBU]}" "" ALLOW "Fall 12: NOBG blocks the member only for foreign jobs: own job visible"
jw_expect NOBU "${JID[OWNX]}" OWNX HIDE "Fall 12: the NOBG member does not see foreign jobs"
jw_expect JWN0 "${JID[OWNX]}" OWNX ALLOW "Fall 12: a non-member reads foreign jobs"
jw_dele_case NOBU NOBU OK "Fall 12: the NOBG member deletes the own job"
jw_dele_case JWN0 OWNX NOCANCEL "Fall 12: a reader does not delete"

# ============================================================
# Fall 13: invalid characters in an owner
# ============================================================
jw_skip "Fall 13: the container cannot produce an owner with invalid characters (covered by TSTJWN)"

# ============================================================
# Fall 14: two jobs with the same 8-character prefix (hard condition for the cache)
# ============================================================
jw_fx 14
JW_F0=$FAILED
jw_m AUDU ABCDEFG "Fall 14" PRODX:ALLOW TESTX:ALLOW
if [ "$FAILED" = "$JW_F0" ]; then
    pass "Fall 14: PRODX and TESTX get the same level (W = 8); the cache may follow (N5)"
else
    fail "Fall 14: the two jobs are treated differently; there is no cache (N5)"
fi

# ============================================================
# Fall 17: STC logs with and without the SYSTEM.* line
# ============================================================
if jw_have STC; then
    jw_fx A
    jw_expect AUDU "${JID[STC]}" '*' HIDE  "Fall 17: example A hides the STC job from the auditor"
    jw_expect AUDU "${JID[JWN0]}" '*' ALLOW "Fall 17: example A shows other jobs to the auditor"
    jw_fx B
    jw_expect JWN0 "${JID[STC]}" '*' HIDE  "Fall 17: example B hides the STC job from the reader"
    jw_fx Bn
    jw_expect JWN0 "${JID[STC]}" '*' ALLOW "Fall 17: without the SYSTEM.* line the STC job is visible"
else
    jw_fx B
fi
jw_dele_case JWN0 OWNX NOCANCEL "Fall 17: example B, a reader does not delete"
jw_dele_case OPSX OWNX OK "Fall 17: example B, OPS deletes"

# ============================================================
# Fall 19: D, the first target configuration
# ============================================================
jw_fx D
for a in ADMA ADMB; do
    jw_owner_is $a '*' 200 "Fall 19: $a may use JESOWNER=*"
    jw_m $a '*' "Fall 19 ($a)" IBMUSER:ALLOW JWN0:ALLOW MIKE10:ALLOW
    jw_have STC && jw_expect $a "${JID[STC]}" '*' ALLOW "Fall 19 ($a): the STC job is visible"
    jw_have NOOWN && jw_expect $a "${JID[NOOWN]}" '*' ALLOW "Fall 19 ($a): the job without owner is visible"
done
jw_dele_case ADMA OWNX OK "Fall 19: ADMA (default group ADMIN) deletes a foreign job"
jw_dele_case ADMB JWN0 OK "Fall 19: ADMB (ADMIN only connected) deletes a foreign job"
jw_expect OPSU "${JID[OPSU]}" "" ALLOW "Fall 19: OPERATIONS sees the own job"
jw_expect OPSU "${JID[JWN0]}" JWN0 HIDE "Fall 19: OPERATIONS is no bypass"
jw_dele_case OPSU OWNX HIDE "Fall 19: OPERATIONS does not delete a foreign job"
jw_dele_case OPSU OPSU OK "Fall 19: OPERATIONS deletes the own job"
# Fall 8, wildcard DELE with the right to delete.
jw_ids ADMA '*' > "$TMPDIR/jw_ids.before"
jw_ftp ADMA "site filetype=jes
--
delete JOB*
--
delete job*
--
delete *"
JW_W=1
for n in 3 4 5; do jw_seg $n | grep -q '^\(501\|550\)' || JW_W=0; done
jw_ids ADMA '*' > "$TMPDIR/jw_ids.after"
if [ $JW_W = 1 ] && cmp -s "$TMPDIR/jw_ids.before" "$TMPDIR/jw_ids.after"; then
    pass "Fall 8: DELE JOB*, job* and * by an administrator are refused and remove no job"
else
    fail "Fall 8: DELE JOB* by an administrator was not refused or changed the job list"
fi
jw_mkjobs missing

# ============================================================
# Fall 20: E, the second target configuration
# ============================================================
jw_fx E
jw_owner_is JWN0 '*' 200 "Fall 20: D may use JESOWNER=*"
jw_expect JWN0 "${JID[OWNX]}" '*' ALLOW "Fall 20: D reads a foreign job"
jw_dele_case JWN0 OWNX NOCANCEL "Fall 20: D does not delete a foreign job"
jw_dele_case JWN0 JWN0 OK "Fall 20: D deletes the own job"
jw_dele_case ADMA OWNX OK "Fall 20: ADMA deletes a foreign job"
jw_have STC && jw_expect JWN0 "${JID[STC]}" '*' HIDE "Fall 20: the STC job is hidden from D"
jw_have STC && jw_expect ADMA "${JID[STC]}" '*' HIDE "Fall 20: the STC job is hidden from ADMA"
if jw_have STC; then
    jw_fx E2
    jw_expect ADMA "${JID[STC]}" '*' ALLOW "Fall 20: with the ADMIN line in SYSTEM.* ADMA sees the STC job"
    jw_expect JWN0 "${JID[STC]}" '*' HIDE "Fall 20: with the ADMIN line D still does not"
fi

# ============================================================
# Fall 18 and 21: the pair MIKE1/MIKE2 and the prefix trap
# ============================================================
jw_fx F
jw_expect MIKE1 "${JID[MIKE1]}" "" ALLOW "Fall 18: MIKE1 sees its own job"
jw_expect MIKE1 "${JID[MIKE10]}" MIKE10 HIDE "Fall 18: MIKE1.* does not reach MIKE10"
jw_expect MIKE10 "${JID[MIKE10]}" "" ALLOW "Fall 18: MIKE10 sees its own job"
jw_expect MIKE10 "${JID[MIKE1]}" MIKE1 HIDE "Fall 18: MIKE10 does not see the job of MIKE1"
jw_dele_case MIKE1 MIKE10 HIDE "Fall 18: DELE of the job of MIKE10 by MIKE1"
jw_dele_case MIKE10 MIKE1 HIDE "Fall 18: DELE of the job of MIKE1 by MIKE10"
jw_expect MIKE1 "${JID[MIKE2]}" MIKE2 ALLOW "Fall 21: MIKE1 reads the job of MIKE2"
jw_expect MIKE2 "${JID[MIKE1]}" MIKE1 ALLOW "Fall 21: MIKE2 reads the job of MIKE1"
jw_dele_case MIKE1 MIKE2 OK "Fall 21: MIKE1 deletes a job of MIKE2"
jw_dele_case MIKE2 MIKE1 OK "Fall 21: MIKE2 deletes a job of MIKE1"
jw_dele_case MIKE1 MIKE1 OK "Fall 21: MIKE1 deletes its own job"
jw_m ADMA '*' "Fall 21 (F)" MIKE1:ALLOW MIKE2:ALLOW MIKE10:ALLOW IBMUSER:ALLOW
jw_dele_case ADMA MIKE2 OK "Fall 21: ADMA deletes a job of MIKE2"
jw_fx Fv
jw_m ADMA '*' "Fall 21 (without ADMIN lines)" MIKE1:HIDE MIKE2:HIDE MIKE10:ALLOW IBMUSER:ALLOW
jw_have STC && jw_expect ADMA "${JID[STC]}" '*' ALLOW "Fall 21 (without ADMIN lines): the STC job is visible"
jw_have NOOWN && jw_expect ADMA "${JID[NOOWN]}" '*' ALLOW "Fall 21 (without ADMIN lines): the job without owner is visible"
jw_expect MIKE1 "${JID[MIKE2]}" MIKE2 ALLOW "Fall 21 (without ADMIN lines): MIKE1 still reads MIKE2"
jw_expect MIKE2 "${JID[MIKE1]}" MIKE1 ALLOW "Fall 21 (without ADMIN lines): MIKE2 still reads MIKE1"

# ============================================================
# Fall 11: a changed profile is in effect for the next command
# ============================================================
JW_LIVE_OUT="$TMPDIR/jw_live.out"
jw_live_start() {   # user
    JW_FIFO="$TMPDIR/jw_fifo"
    rm -f "$JW_FIFO" "$JW_LIVE_OUT"; mkfifo "$JW_FIFO"
    ftp -n -v -p -P "$PORT" "$HOST" < "$JW_FIFO" > "$JW_LIVE_OUT" 2>&1 &
    JW_LIVE_PID=$!
    exec 8> "$JW_FIFO"
    printf 'user %s %s\n' "$1" "$JW_PW" >&8
    jw_live_send "site filetype=jes" > /dev/null
    jw_live_send "type ascii" > /dev/null
}
# Sends a command and prints its replies (up to the next marker).
jw_live_send() {
    local k n=0
    k=$(grep -c '^200 OK' "$JW_LIVE_OUT")
    printf '%s\nquote NOOP\n' "$1" >&8
    while [ $n -lt 60 ]; do
        [ "$(grep -c '^200 OK' "$JW_LIVE_OUT")" -gt "$k" ] && break
        sleep 1; n=$((n + 1))
    done
    awk -v k="$k" '/^200 OK/ { n++; next } n == k { print }' "$JW_LIVE_OUT"
}
jw_live_stop() {
    printf 'quit\n' >&8
    exec 8>&-
    wait "$JW_LIVE_PID" 2>/dev/null
}

jw_live_case() {   # label
    local r g
    jw_fx N
    jw_live_start AUDU
    r=$(jw_live_send "site jesowner=*" | grep -o '^[0-9][0-9][0-9]' | tail -1)
    [ "$r" = 501 ] && pass "$1: before the change JESOWNER=* is refused" \
                   || fail "$1: before the change JESOWNER=* gave '$r'"
    rm -f "$TMPDIR/jw_got11"
    jw_live_send "get ${JID[JWN0]} $TMPDIR/jw_got11" > /dev/null
    [ -s "$TMPDIR/jw_got11" ] && fail "$1: before the change the foreign job was read" \
                              || pass "$1: before the change the foreign job is not read"
    jw_fx 4 || fail "$1: the change could not be loaded"
    r=$(jw_live_send "site jesowner=*" | grep -o '^[0-9][0-9][0-9]' | tail -1)
    [ "$r" = 200 ] && pass "$1: after the change JESOWNER=* is accepted in the same session" \
                   || fail "$1: after the change JESOWNER=* gave '$r' in the same session"
    jw_live_send "get ${JID[JWN0]} $TMPDIR/jw_got11" > /dev/null
    [ -s "$TMPDIR/jw_got11" ] && pass "$1: after the change the foreign job is read in the same session" \
                              || fail "$1: after the change the foreign job is still not read"
    jw_live_stop
}
jw_live_case "Fall 11 (batch reload)"
if [ -n "$FTPD_CONSOLE" ]; then
    JW_RELOAD=console
    jw_live_case "Fall 11 (console reload)"
    JW_RELOAD=batch
else
    jw_skip "Fall 11 (console reload): FTPD_CONSOLE is not set"
fi

# ============================================================
# Fall 22: a reload without the universal line
# ============================================================
jw_broken_case() {   # label
    jw_fx D
    jw_expect ADMA "${JID[JWN0]}" '*' ALLOW "$1: before the reload ADMA sees the job"
    if jw_fx BROKEN bad; then
        pass "$1: the reload reports RAKFPROF4 PERMIT RECORD WITHOUT UNIVERSAL ACCESS RECORD"
    else
        fail "$1: the reload did not report RAKFPROF4 PERMIT RECORD WITHOUT UNIVERSAL ACCESS RECORD"
        grep 'RAKFPROF' "$TMPDIR/jw_tso.out" | head -4 | sed 's/^/    /'
    fi
    JW_CUR=""
    grep -q 'U0400' "$TMPDIR/jw_tso.out" && pass "$1: the reload ends with ABEND S000 U0400" \
        || fail "$1: no U0400 in the output of the reload"
    jw_expect ADMA "${JID[JWN0]}" '*' ALLOW "$1: the old chain stays in effect"
    jw_fx D && pass "$1: the correct file loads again (RAKFPROF7)" \
            || fail "$1: the correct file does not load again"
}
jw_broken_case "Fall 22 (batch reload)"
if [ -n "$FTPD_CONSOLE" ]; then
    JW_RELOAD=console
    jw_broken_case "Fall 22 (console reload)"
    JW_RELOAD=batch
else
    jw_skip "Fall 22 (console reload): FTPD_CONSOLE is not set"
fi

# --------------------------------------------------------------------
# Clean up: every owner removes the own jobs, an administrator the rest
# --------------------------------------------------------------------
jw_fx D
for k in "${!JID[@]}"; do
    case "$k" in STC) continue ;; esac
    [ -n "${JID[$k]}" ] || continue
    jw_del ADMA "${JID[$k]}"
done

fi   # JW_SETUP

jw_restore
if [ "$JW_NFIND" -gt 0 ]; then
    echo -e "\n  ${YELLOW}FINDINGS${NC} (plan, not server): $JW_NFIND$(echo -e "$JW_FINDINGS")"
    [ "$JW_GATE" = 1 ] && fail "gate: $JW_NFIND finding(s) about the examples of the plan are open"
fi
if [ -n "$JW_SKIPPED" ]; then
    echo -e "\n  ${YELLOW}SKIPPED${NC} cases (for the release notes):$(echo -e "$JW_SKIPPED")"
fi

# ============================================================
# Test 19: LIST and NLST of 'DSN(pattern)' (#302)
#
# The Zowe FTP plugin lists the members of a PDS, and looks for a member
# before it downloads one, with LIST 'DSN(*)' from whatever the working
# directory is.  FTPD used to take that for a data set prefix and answer an
# empty data set list and 226, which the plugin cannot parse.
# ============================================================
section "Test 19: LIST and NLST of 'DSN(pattern)'"

LMP_PDS="${HLQ}.TEST.LMPPDS"
LMP_SEQ="${HLQ}.TEST.LMPSEQ"
LMP_TEXT="$TMPDIR/lmp_text.txt"
LMP_OUT="$TMPDIR/ftp_lmp.log"
printf 'one\ntwo\n' > "$LMP_TEXT"

ftp_run "rmdir '$LMP_PDS'
delete '$LMP_SEQ'" /dev/null
ftp_run "mkdir '$LMP_PDS'
type ascii
put $LMP_TEXT '$LMP_PDS(ALPHA)'
put $LMP_TEXT '$LMP_PDS(ALPHB)'
put $LMP_TEXT '$LMP_PDS(BETA)'
put $LMP_TEXT '$LMP_SEQ'" "$TMPDIR/ftp_lmp_setup.log"

# Expect the member names of one command's listing, in order, and nothing else.
lmp_expect() {
    local cmds="$1" want="$2" label="$3" got
    ftp_run "type ascii
$cmds" "$LMP_OUT"
    got=$(sed -n '/^150 /,/^226 /p' "$LMP_OUT" | grep -v '^1[0-9][0-9] \|^226 \|^ Name ' \
          | awk '{ printf "%s ", $1 }' | sed 's/ $//')
    if [ "$got" = "$want" ]; then
        pass "$label"
    else
        fail "$label: got '$got', wanted '$want'"; tail -8 "$LMP_OUT" | sed 's/^/    /'
    fi
}

lmp_expect "ls '$LMP_PDS(*)'" "ALPHA ALPHB BETA" \
    "NLST 'DSN(*)' from the HLQ lists the members"
lmp_expect "dir '$LMP_PDS(*)'" "ALPHA ALPHB BETA" \
    "LIST 'DSN(*)' from the HLQ lists the members"
lmp_expect "ls '$LMP_PDS(AL*)'" "ALPHA ALPHB" "a member pattern narrows the list"
lmp_expect "ls '$LMP_PDS(BETA)'" "BETA" "a member name lists that member"
lmp_expect "ls '$LMP_PDS(al*)'" "ALPHA ALPHB" "the pattern is case-insensitive"
lmp_expect "cd TEST
ls LMPPDS(*)" "ALPHA ALPHB BETA" "DSN(pattern) is relative to the working directory"
lmp_expect "cd '$LMP_SEQ'
ls '$LMP_PDS(*)'" "ALPHA ALPHB BETA" "the working directory does not matter"
lmp_expect "cd '$LMP_PDS'
ls '$LMP_PDS(B*)'" "BETA" "a quoted DSN(pattern) works inside another PDS context"
lmp_expect "cd '$LMP_PDS'
ls" "ALPHA ALPHB BETA" "cd into the PDS and ls: unchanged"
lmp_expect "cd '$LMP_PDS'
ls AL*" "ALPHA ALPHB" "cd into the PDS and ls with a pattern: unchanged"
lmp_expect "nlist '$LMP_PDS'" "TEST.LMPPDS" "NLST of the PDS name without parentheses: still a data set listing"

ftp_run "type ascii
dir '$LMP_PDS(*)'" "$LMP_OUT"
if grep -q '^ Name     VV.MM ' "$LMP_OUT"; then
    pass "LIST 'DSN(*)' starts with the member header"
else
    fail "LIST 'DSN(*)' has no member header"; tail -6 "$LMP_OUT" | sed 's/^/    /'
fi

for LMP_BAD in "'$LMP_PDS(ZZ*)'" "'${HLQ}.TEST.NOSUCH(*)'" "'$LMP_SEQ(*)'"; do
    ftp_run "type ascii
ls $LMP_BAD" "$LMP_OUT"
    if grep -q '^550 No data sets found\.' "$LMP_OUT" && ! grep -q '^226 ' "$LMP_OUT"; then
        pass "ls $LMP_BAD: 550 No data sets found."
    else
        fail "ls $LMP_BAD"; tail -4 "$LMP_OUT" | sed 's/^/    /'
    fi
done

ftp_run "quote SITE FILETYPE=SEQ ISPFSTATS" "$LMP_OUT"
if grep -q '^200 SITE command was accepted' "$LMP_OUT" && ! grep -q 'Unrecognized' "$LMP_OUT"; then
    pass "SITE ISPFSTATS is accepted"
else
    fail "SITE ISPFSTATS"; tail -3 "$LMP_OUT" | sed 's/^/    /'
fi

ftp_run "rmdir '$LMP_PDS'
delete '$LMP_SEQ'" /dev/null

# ============================================================
# Test 20: names with a '(' in CWD, LIST and the other commands (#335)
#
# The check of a data set name used to stop at the first '(' and never read
# the rest.  A member is exactly "(NAME)" at the end of a name now, and CWD,
# which sets a prefix, takes no member at all.
# ============================================================
section "Test 20: parentheses in names (#335)"

PAR_PDS="${HLQ}.TEST.PARPDS"
PAR_OUT="$TMPDIR/ftp_par.log"

ftp_run "rmdir '$PAR_PDS'" /dev/null
ftp_run "mkdir '$PAR_PDS'
type ascii
put $LMP_TEXT '$PAR_PDS(ALPHA)'" "$PAR_OUT"

# One command that has to be refused with 501 and has to leave the working
# directory where the previous cd put it.
par_refused() {
    local cmd="$1" label="$2"
    ftp_run "cd '${PAR_PDS}.'
$cmd
pwd" "$PAR_OUT"
    if grep -q '^501 ' "$PAR_OUT" && ! grep -q '^226 ' "$PAR_OUT" \
       && grep -q "^257 .*${PAR_PDS}\." "$PAR_OUT"; then
        pass "$label: 501, working directory unchanged"
    else
        fail "$label"; tail -6 "$PAR_OUT" | sed 's/^/    /'
    fi
}

par_refused "cd '$PAR_PDS(ALPHA)'" "CWD to DSN(MEMBER)"
par_refused "cd '$PAR_PDS(ALPHA'" "CWD to a member that is not closed"
par_refused "cd '${PAR_PDS}.X(Y'" "CWD to a prefix with an open parenthesis"
par_refused "ls '$PAR_PDS(ALPHA'" "ls of a member that is not closed"
par_refused "ls '$PAR_PDS(ALPHA)(B)'" "ls of two members"
par_refused "ls '$PAR_PDS(ALPHA)X'" "ls with text after the member"
par_refused "quote DELE '$PAR_PDS(AL PHA)'" "DELE of a member holding a blank"
par_refused "quote DELE '$PAR_PDS(AL'PHA)'" "DELE of a member holding a quote"

# The members and the listing forms that worked before still do.
ftp_run "type ascii
ls '$PAR_PDS(*)'
cd '$PAR_PDS'
ls
cd '${HLQ}.'
ls '$PAR_PDS(ALPHA)'" "$PAR_OUT"
if [ "$(grep -c '^226 ' "$PAR_OUT")" = 3 ] && grep -q '^250 .*partitioned data set' "$PAR_OUT" \
   && ! grep -q '^501 ' "$PAR_OUT"; then
    pass "DSN(pattern), ls in a PDS and a quoted member still list"
else
    fail "DSN(pattern), ls in a PDS and a quoted member still list"; tail -12 "$PAR_OUT" | sed 's/^/    /'
fi

ftp_run "cd '$PAR_PDS'
get ALPHA $TMPDIR/par_alpha.txt
cd '${HLQ}.'
delete '$PAR_PDS(ALPHA)'" "$PAR_OUT"
if [ -s "$TMPDIR/par_alpha.txt" ] && grep -q '^250 ' "$PAR_OUT" && ! grep -q '^501 ' "$PAR_OUT"; then
    pass "get a member from the PDS and delete DSN(MEMBER): unchanged"
else
    fail "get a member from the PDS and delete DSN(MEMBER)"; tail -8 "$PAR_OUT" | sed 's/^/    /'
fi

ftp_run "rmdir '$PAR_PDS'" /dev/null

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
