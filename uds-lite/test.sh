#!/usr/bin/env bash
# test.sh — uds-lite 综合自动化测试脚本
# 验证 server、client、shell 三个组件的完整功能。
# 用法: chmod +x test.sh && ./test.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build/x86-64"
SERVER="$BUILD_DIR/uds_server"
CLIENT="$BUILD_DIR/uds_client"
SHELL="$BUILD_DIR/uds_shell"

# 使用 PID 拼接唯一端口，避免与其他实例冲突
PORT=$((22400 + ($$ % 4000)))

PASS=0
FAIL=0
SERVER_PID=""

# ── 清理函数 ──────────────────────────────────
cleanup() {
    if [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT INT TERM

# ── 辅助函数 ──────────────────────────────────
pass() {
    echo "  [PASS] $1"
    PASS=$((PASS + 1))
}

fail() {
    echo "  [FAIL] $1"
    FAIL=$((FAIL + 1))
}

# ── 检查二进制文件 ────────────────────────────
check_binary() {
    if [ -x "$1" ]; then
        echo "  [OK]  Found: $1"
    else
        echo "  [ERR] Missing: $1"
        exit 1
    fi
}

# 启动服务器
start_server() {
    echo ""
    echo "=== Starting Server ==="
    check_binary "$SERVER"
    check_binary "$CLIENT"
    check_binary "$SHELL"

    "$SERVER" "$PORT" &
    SERVER_PID=$!
    echo "  Server PID: $SERVER_PID on port $PORT"

    # 等待服务器就绪（最多 3 秒）
    local retries=0
    while [ $retries -lt 30 ]; do
        if nc -z 127.0.0.1 "$PORT" 2>/dev/null; then
            echo "  [OK]  Server is listening"
            return 0
        fi
        sleep 0.1
        retries=$((retries + 1))
    done
    echo "  [ERR] Server failed to start"
    exit 1
}

# ── 测试运行函数 ──────────────────────────────
# $1: 测试名称, $2: 命令 (在子shell中运行)

# 测试客户端完整工作流
test_client_workflow() {
    echo ""
    echo "=== Test: Client Full Workflow ==="
    local out
    if out=$("$CLIENT" "127.0.0.1" "$PORT" 2>&1); then
        local rc=$?
        # 检查输出中是否包含完成标记
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

# 测试通过管道发送 shell 命令
test_shell_command() {
    local name="$1"
    shift
    echo ""
    echo "=== Test: Shell — $name ==="
    local out
    # 使用 timeout 防止 shell 挂死；用 echo 管道发送命令
    # quit 命令确保 shell 正常退出
    local cmd_str=""
    for c in "$@"; do
        cmd_str="${cmd_str}${c}"$'\n'
    done
    cmd_str="${cmd_str}quit"$'\n'

    if out=$(echo "$cmd_str" | "$SHELL" "127.0.0.1" "$PORT" 2>&1); then
        # shell 返回 0 说明连接和命令执行正常（uds_shell 始终返回 0 除非 connect 失败）
        pass "$name"
        # 打印响应摘要（最后几行）
        echo "$out" | grep -E '(Rx |POSITIVE|NEGATIVE)' | tail -4
        return 0
    else
        local rc=$?
        fail "$name (connect failed, exit=$rc)"
        echo "$out" | tail -5
        return 1
    fi
}

# 测试带响应验证的 shell 命令
test_shell_verify() {
    local name="$1"
    local pattern="$2"
    shift 2
    echo ""
    echo "=== Test: Shell — $name ==="
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

# ── 主测试序列 ─────────────────────────────────

echo "#############################################"
echo "  uds-lite Automated Test Suite"
echo "#############################################"
echo "  Port: $PORT"
echo "  Build: $BUILD_DIR"
echo ""

# 1. 启动服务器
start_server
pass "Server startup"

# 2. 完整客户端工作流
test_client_workflow

# 3. Shell: 会话控制
test_shell_verify "SessionControl — Extended" "POSITIVE.*0x10" \
    "session 03"

# 4. Shell: 安全访问 Seed/Key
test_shell_verify "SecurityAccess — RequestSeed" "POSITIVE.*0x27" \
    "security"

# 在同一个 shell 会话中完成 Seed/Key 完整流程
test_shell_verify "SecurityAccess — Full Seed/Key" "POSITIVE.*0x27" \
    "security" \
    "sendkey F9 8E 05 48"

# 5. Shell: 读取 DID
test_shell_verify "ReadDID 0x010C — Engine RPM" "POSITIVE.*0x22" \
    "session 03" \
    "read 010c"

test_shell_verify "ReadDID 0xF190 — VIN" "POSITIVE.*0x22" \
    "session 03" \
    "security" \
    "sendkey F9 8E 05 48" \
    "read f190"

# 6. Shell: 读取 DTC
test_shell_verify "ReadDTC — DTC Count" "POSITIVE.*0x19" \
    "session 03" \
    "dtc"

# 7. Shell: TesterPresent
test_shell_verify "TesterPresent" "POSITIVE.*0x3E" \
    "tp"

# 8. Shell: 会话控制（编程会话）
test_shell_verify "SessionControl — Programming" "POSITIVE.*0x10" \
    "session 02"

# 9. Shell: 下行下载序列
test_shell_verify "RequestDownload 0x34" "POSITIVE.*0x34" \
    "session 02" \
    "download 40000 100"

# 完整的下载三部曲
test_shell_verify "Download — Full Sequence" "POSITIVE.*0x37" \
    "session 02" \
    "download 40000 100" \
    "xfer 01" \
    "xexit"

# 10. Shell: RoutineControl
test_shell_verify "RoutineControl — Start" "POSITIVE.*0x31" \
    "session 03" \
    "routine 0201"

# ── 统计结果 ───────────────────────────────────
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
