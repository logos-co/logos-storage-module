#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# Logos Storage Module — Integration Test Suite
#
# Tests vital operations: start, peerId, upload, download, stop.
#
# All commands that share state run in a single logoscore invocation.
# Upload and download are split across two invocations because download
# needs to wait for the upload to complete — the data-dir persists on disk
# between runs.
#
# Usage: run_tests.sh <logoscore> <storage-module-dir>
# ─────────────────────────────────────────────────────────────────────────────
set -uo pipefail

LOGOSCORE="${1:?Usage: run_tests.sh <logoscore> <storage-module-dir>}"
STORAGE_DIR="${2:?}"

# Per-call timeout (seconds).
CALL_TIMEOUT="${TEST_TIMEOUT:-60}"

# ── Check --quit-on-finish ────────────────────────────────────────────────────
if "$LOGOSCORE" --help 2>&1 | grep -q "quit-on-finish"; then
    QUIT_FLAG="--quit-on-finish"
    echo "  quit-flag : --quit-on-finish (detected)"
else
    echo "ERROR: logoscore does not support --quit-on-finish." >&2
    exit 1
fi

# ── Temp setup ────────────────────────────────────────────────────────────────
DATA_DIR=$(mktemp -d)
TEST_FILE="$DATA_DIR/upload.txt"
DOWNLOAD_FILE="$DATA_DIR/download.txt"
TEST_CONTENT="Hello, Logos Storage Integration Test!"
printf '%s' "$TEST_CONTENT" > "$TEST_FILE"

TEST_FILE_URL="file://$TEST_FILE"
DOWNLOAD_URL="file://$DOWNLOAD_FILE"
CONFIG="{\"data-dir\": \"$DATA_DIR\", \"log-level\": \"WARN\"}"

cleanup() { rm -rf "$DATA_DIR"; }
trap cleanup EXIT

PASS=0; FAIL=0; SKIP=0; TOTAL=0; FAILURES=""

# ── Helpers ───────────────────────────────────────────────────────────────────

assert_in_output() {
    local name="$1" expected="$2" output="$3"
    TOTAL=$((TOTAL + 1))
    if printf '%s' "$output" | grep -qF "$expected"; then
        PASS=$((PASS + 1))
        printf "  PASS  %s\n" "$name"
        return 0
    else
        FAIL=$((FAIL + 1))
        printf "  FAIL  %s  (expected '%s' not found)\n" "$name" "$expected"
        FAILURES="${FAILURES}  FAIL  ${name}\n"
        return 1
    fi
}

skip_test() {
    SKIP=$((SKIP + 1))
    printf "  SKIP  %s  (%s)\n" "$1" "$2"
}

# Run a single logoscore invocation and return its output.
run_storage() {
    local output rc
    # shellcheck disable=SC2086
    output=$(timeout "$CALL_TIMEOUT" "$LOGOSCORE" $QUIT_FLAG \
        -m "$STORAGE_DIR" -l storage_module \
        "$@" 2>/dev/null) && rc=0 || rc=$?
    printf '%s' "$output"
    return $rc
}

# ── Banner ────────────────────────────────────────────────────────────────────
echo "================================================================="
echo " Logos Storage Module -- Integration Tests"
echo "================================================================="
echo ""
echo "  logoscore   : $LOGOSCORE"
echo "  storage-dir : $STORAGE_DIR"
echo "  data-dir    : $DATA_DIR"
echo "  test-file   : $TEST_FILE"
echo ""

# ═════════════════════════════════════════════════════════════════════════════
# RUN 1: start → peerId → upload
#
# version() is called between start() and peerId() to give the event loop
# time to process the async node start before peerId() is called.
# manifests() is called 3 times after uploadUrl() to give the async upload
# time to complete and to retrieve the resulting CID.
# ═════════════════════════════════════════════════════════════════════════════
echo "-----------------------------------------------------------------"
echo " Run 1: start, peerId, upload"
echo "-----------------------------------------------------------------"
echo ""

run1=$(run_storage \
    -c "storage_module.init($CONFIG)" \
    -c "storage_module.start()" \
    -c "storage_module.version()" \
    -c "storage_module.peerId()" \
    -c "storage_module.uploadUrl($TEST_FILE_URL)" \
    -c "storage_module.manifests()" \
    -c "storage_module.manifests()" \
    -c "storage_module.manifests()" \
    -c "storage_module.stop()") && run1_rc=0 || run1_rc=$?

if [[ $run1_rc -ne 0 ]]; then
    printf "  WARN  Run 1 exited with code %d\n" "$run1_rc"
fi

echo "  -- start --"
assert_in_output "start()" "true" "$run1"

echo ""
echo "  -- peerId --"
TOTAL=$((TOTAL + 1))
PEER_ID=$(printf '%s' "$run1" | grep -o 'Result: [^ ]*' | head -1 | sed 's/Result: //')
if [[ -n "$PEER_ID" ]]; then
    PASS=$((PASS + 1))
    printf "  PASS  peerId()  (%s)\n" "$PEER_ID"
else
    FAIL=$((FAIL + 1))
    printf "  FAIL  peerId()  (empty or not found)\n"
    FAILURES="${FAILURES}  FAIL  peerId()\n"
fi

echo ""
echo "  -- upload --"
TOTAL=$((TOTAL + 1))
# CIDs in Codex are base32 CIDv1: start with "bafy" followed by base32 chars.
CID=$(printf '%s' "$run1" | grep -o 'bafy[a-z2-7]*' | head -1)
if [[ -n "$CID" ]]; then
    PASS=$((PASS + 1))
    printf "  PASS  uploadUrl() completed  (CID: %s)\n" "$CID"
else
    FAIL=$((FAIL + 1))
    printf "  FAIL  uploadUrl() — CID not found in manifests output\n"
    FAILURES="${FAILURES}  FAIL  uploadUrl: CID not found in manifests\n"
fi

echo ""
echo "  -- stop --"
assert_in_output "stop()" "true" "$run1"

# ═════════════════════════════════════════════════════════════════════════════
# RUN 2: download
#
# Re-initialises the node against the same data-dir so the uploaded blocks
# are available, then downloads the file and verifies the content matches.
# ═════════════════════════════════════════════════════════════════════════════
echo ""
echo "-----------------------------------------------------------------"
echo " Run 2: download"
echo "-----------------------------------------------------------------"
echo ""

echo "  -- download --"

if [[ -z "$CID" ]]; then
    skip_test "downloadToUrl()" "no CID from upload — skipping"
else
    run2=$(run_storage \
        -c "storage_module.init($CONFIG)" \
        -c "storage_module.start()" \
        -c "storage_module.downloadToUrl($CID, $DOWNLOAD_URL)" \
        -c "storage_module.stop()") && run2_rc=0 || run2_rc=$?

    TOTAL=$((TOTAL + 1))
    if [[ -f "$DOWNLOAD_FILE" ]]; then
        downloaded=$(cat "$DOWNLOAD_FILE")
        if [[ "$downloaded" == "$TEST_CONTENT" ]]; then
            PASS=$((PASS + 1))
            printf "  PASS  downloadToUrl()  (content matches)\n"
        else
            FAIL=$((FAIL + 1))
            printf "  FAIL  downloadToUrl()  (content mismatch)\n"
            printf "        expected : '%s'\n" "$TEST_CONTENT"
            printf "        got      : '%s'\n" "$downloaded"
            FAILURES="${FAILURES}  FAIL  downloadToUrl: content mismatch\n"
        fi
    else
        FAIL=$((FAIL + 1))
        printf "  FAIL  downloadToUrl()  (file not created, exit %d)\n" "$run2_rc"
        FAILURES="${FAILURES}  FAIL  downloadToUrl: file not created\n"
    fi
fi

# ═════════════════════════════════════════════════════════════════════════════
# Summary
# ═════════════════════════════════════════════════════════════════════════════
echo ""
echo "================================================================="
echo " Results: $PASS passed, $FAIL failed, $SKIP skipped (of $TOTAL run)"
echo "================================================================="

if [[ $FAIL -gt 0 ]]; then
    echo ""
    echo "Failures:"
    printf "%b" "$FAILURES"
    echo ""
    exit 1
fi

echo ""
echo "All tests passed."
exit 0
