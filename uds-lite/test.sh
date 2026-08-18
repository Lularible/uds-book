#!/usr/bin/env bash
# test.sh -- uds-lite comprehensive automated test script
# Verifies the full functionality of the server, client and shell components.
# Usage: chmod +x test.sh && ./test.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build/x86-64"
SERVER="$BUILD_DIR/uds_server"
CLIENT="$BUILD_DIR/uds_client"
SHELL="$BUILD_DIR/uds_shell"

# Build a unique port from the PID to avoid conflicts with other instances
PORT=$((22400 + ($$ % 4000)))

PASS=0
FAIL=0
SERVER_PID=""

# -- Cleanup function ------------------------------------
cleanup() {
    if [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT INT TERM

# -- Helper functions ------------------------------------
pass() {
    echo "  [PASS] $1"
    PASS=$((PASS + 1))
}

fail() {
    echo "  [FAIL] $1"
    FAIL=$((FAIL + 1))
}

# -- Check binaries exist --------------------------------
check_binary() {
    if [ -x "$1" ]; then
        echo "  [OK]  Found: $1"
    else
        echo "  [ERR] Missing: $1"
        exit 1
    fi
}

# Start the server
start_server() {
    echo ""
    echo "=== Starting Server ==="
    check_binary "$SERVER"
    check_binary "$CLIENT"
    check_binary "$SHELL"

    "$SERVER" "$PORT" &
    SERVER_PID=$!
    echo "  Server PID: $SERVER_PID on port $PORT"

    # Wait for the server to be ready (max 3 seconds)
    local retries=0
    while [ $retries -lt 30 ]; do
        if command -v nc >/dev/null 2>&1; then
            nc -z 127.0.0.1 "$PORT" 2>/dev/null
        else
            # Fall back to bash's built-in /dev/tcp port probe when netcat is absent
            timeout 1 bash -c "exec 3<>/dev/tcp/127.0.0.1/$PORT" 2>/dev/null
        fi
        if [ $? -eq 0 ]; then
            echo "  [OK]  Server is listening"
            return 0
        fi
        sleep 0.1
        retries=$((retries + 1))
    done
    echo "  [ERR] Server failed to start"
    exit 1
}

# -- Test runner functions -------------------------------
# $1: test name, $2: command (run in a subshell)

# Test the full client workflow
test_client_workflow() {
    echo ""
    echo "=== Test: Client Full Workflow ==="
    local out
    if out=$("$CLIENT" "127.0.0.1" "$PORT" 2>&1); then
        local rc=$?
        # Check whether the output contains the completion marker
        if echo "$out" | grep -q "workflow complete"; then
            pass "Client workflow exited 0 and completed"
        else
            fail "Client exited 0 but missing 'workflow complete'"
            echo "$out" | head -20
        fi
        return 0
    else
        local rc=$?
        fail "Client workflow failed (exit=$rc)"
        echo "$out" | head -20
        return 1
    fi
}

# Test shell commands sent through a pipe
test_shell_command() {
    local name="$1"
    shift
    echo ""
    echo "=== Test: Shell -- $name ==="
    local out
    # Use timeout to prevent the shell from hanging; use echo pipe to send commands
    # The quit command ensures the shell exits cleanly
    local cmd_str=""
    for c in "$@"; do
        cmd_str="${cmd_str}${c}"$'\n'
    done
    cmd_str="${cmd_str}quit"$'\n'

    if out=$(echo "$cmd_str" | "$SHELL" "127.0.0.1" "$PORT" 2>&1); then
        # Shell exit 0 means connection and commands worked (uds_shell always returns 0 unless connect fails)
        pass "$name"
        # Print a response summary (last few lines)
        echo "$out" | grep -E '(Rx |POSITIVE|NEGATIVE)' | tail -4
        return 0
    else
        local rc=$?
        fail "$name (connect failed, exit=$rc)"
        echo "$out" | tail -5
        return 1
    fi
}

# Test shell commands with response verification
test_shell_verify() {
    local name="$1"
    local pattern="$2"
    shift 2
    echo ""
    echo "=== Test: Shell -- $name ==="
    local out
    local cmd_str=""
    for c in "$@"; do
        cmd_str="${cmd_str}${c}"$'\n'
    done
    cmd_str="${cmd_str}quit"$'\n'

    if out=$(echo "$cmd_str" | "$SHELL" "127.0.0.1" "$PORT" 2>&1); then
        if echo "$out" | grep -q "$pattern"; then
            pass "$name (matched: $pattern)"
        else
            fail "$name (pattern '$pattern' not found in output)"
            echo "$out" | grep -E '(Rx |POSITIVE|NEGATIVE|Tx )' | tail -6
        fi
        return 0
    else
        fail "$name (connect failed)"
        return 1
    fi
}

# -- Main test sequence -----------------------------------

echo "#############################################"
echo "  uds-lite Automated Test Suite"
echo "#############################################"
echo "  Port: $PORT"
echo "  Build: $BUILD_DIR"
echo ""

# 1. Start the server
start_server
pass "Server startup"

# 2. Full client workflow
test_client_workflow

# 3. Shell: session control
test_shell_verify "SessionControl -- Extended" "POSITIVE.*0x10" \
    "session 03"

# 4. Shell: security access seed/key
test_shell_verify "SecurityAccess -- RequestSeed" "POSITIVE.*0x27" \
    "security"

# Complete the seed/key sequence in the same shell session
test_shell_verify "SecurityAccess -- Full Seed/Key" "POSITIVE.*0x27" \
    "security" \
    "sendkey F9 8E 05 48"

# 5. Shell: read DID
test_shell_verify "ReadDID 0x010C -- Engine RPM" "POSITIVE.*0x22" \
    "session 03" \
    "read 010c"

test_shell_verify "ReadDID 0xF190 -- VIN" "POSITIVE.*0x22" \
    "session 03" \
    "security" \
    "sendkey F9 8E 05 48" \
    "read f190"

# 6. Shell: read DTC
test_shell_verify "ReadDTC -- DTC Count" "POSITIVE.*0x19" \
    "session 03" \
    "dtc"

# 7. Shell: tester present
test_shell_verify "TesterPresent" "POSITIVE.*0x3E" \
    "tp"

# 8. Shell: session control (programming session)
test_shell_verify "SessionControl -- Programming" "POSITIVE.*0x10" \
    "session 02"

# 9. Shell: download sequence
test_shell_verify "RequestDownload 0x34" "POSITIVE.*0x34" \
    "session 02" \
    "download 40000 100"

# Full download triple-step
test_shell_verify "Download -- Full Sequence" "POSITIVE.*0x37" \
    "session 02" \
    "download 40000 100" \
    "xfer 01" \
    "xexit"

# 10. Shell: routine control
test_shell_verify "RoutineControl -- Start" "POSITIVE.*0x31" \
    "session 03" \
    "routine 0201"

# -- Result summary ---------------------------------------
echo ""
echo "============================================="
echo "  Test Results"
echo "============================================="
echo "  Passed: $PASS"
echo "  Failed: $FAIL"
echo ""

if [ "$FAIL" -gt 0 ]; then
    echo "  *** SOME TESTS FAILED ***"
    echo ""
    exit 1
else
    echo "  *** ALL TESTS PASSED ***"
    echo ""
    exit 0
fi
