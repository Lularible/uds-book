/* uds_shell.c — uds-lite 交互式诊断终端
 * 命令行驱动的UDS控制台，支持手工发送任意UDS服务并查看响应。
 * 使用方法: ./uds_shell [ip] [port]
 * 命令: session <01|02|03>  security  read <did>  write <did> <hex...>
 *        dtc [mask]  clear  routine <rid>  download <addr> <size>
 *        tp  help  quit
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>
#include "uds_common.h"
#include "uds_msg.h"

static int sock_fd = -1;
/* ── 网络层 ──────────────────────────────── */
static int connect_ecu(const char *ip, int port)
{
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) { perror("socket"); return -1; }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) { perror("inet_pton"); close(sock_fd); return -1; }
    if (connect(sock_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { perror("connect"); close(sock_fd); return -1; }
    struct timeval tv = {UDS_RX_TIMEOUT_SEC, 0};
    setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    return 0;
}

static void send_and_recv(const u8 *data_in, u16 len)
{
    u8 buf[UDS_MAX_MSG_LEN];
    memcpy(buf, data_in, len);
    if (sock_fd < 0) { printf("  Not connected\n"); return; }
    printf("  Tx [%u]: ", len);
    for (u16 i = 0; i < len; i++) printf("%02X ", buf[i]);
    printf("\n");
    send(sock_fd, buf, len, 0);

    ssize_t n = recv(sock_fd, buf, UDS_MAX_MSG_LEN, 0);
    if (n <= 0) { printf("  No response (timeout)\n"); return; }
    printf("  Rx [%zu]: ", n);
    for (ssize_t i = 0; i < n; i++) printf("%02X ", buf[i]);

    /* 解析响应类型 */
    if (n >= 1 && buf[0] == NEGATIVE_RESPONSE_SID) {
        const char *nrc_str = "Unknown";
        if (n >= 3) {
            switch (buf[2]) {
                case NRC_SERVICE_NOT_SUPPORTED: nrc_str = "ServiceNotSupported"; break;
                case NRC_SUBFUNCTION_NOT_SUPPORTED: nrc_str = "SubFunctionNotSupported"; break;
                case NRC_INCORRECT_MESSAGE_LENGTH: nrc_str = "IncorrectMessageLength"; break;
                case NRC_CONDITIONS_NOT_CORRECT: nrc_str = "ConditionsNotCorrect"; break;
                case NRC_REQUEST_SEQUENCE_ERROR: nrc_str = "RequestSequenceError"; break;
                case NRC_REQUEST_OUT_OF_RANGE: nrc_str = "RequestOutOfRange"; break;
                case NRC_SECURITY_ACCESS_DENIED: nrc_str = "SecurityAccessDenied"; break;
                case NRC_INVALID_KEY: nrc_str = "InvalidKey"; break;
                case NRC_WRONG_BLOCK_SEQ_COUNTER: nrc_str = "WrongBlockSequenceCounter"; break;
                case NRC_RESPONSE_PENDING: nrc_str = "ResponsePending"; break;
                case NRC_SERVICE_NOT_IN_ACTIVE_SESSION: nrc_str = "ServiceNotSupportedInActiveSession"; break;
            }
        }
        printf(" ⚠ NEGATIVE: SID=0x%02X NRC=0x%02X (%s)", buf[1], buf[2], nrc_str);
    } else if (n >= 1 && (buf[0] & SID_POSITIVE_RESPONSE_MASK)) {
        printf(" ✓ POSITIVE: origSID=0x%02X", buf[0] & ~SID_POSITIVE_RESPONSE_MASK);
    }
    printf("\n");
}

/* ── 命令解析 ────────────────────────────── */
static u16 parse_hex(const char *s, u8 *out, u16 max_len)
{
    u16 len = 0;
    const char *p = s;
    while (*p && len < max_len) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        unsigned int val;
        if (sscanf(p, "%2x", &val) == 1) {
            out[len++] = (u8)val;
            p += 2;
        } else {
            p++;
        }
    }
    return len;
}

static u16 parse_did(const char *s)
{
    unsigned int v;
    if (sscanf(s, "%x", &v) == 1) return (u16)v;
    return 0;
}

static void cmd_session(const char *arg)
{
    unsigned int s;
    if (sscanf(arg, "%x", &s) != 1) { printf("  Usage: session <01|02|03>\n"); return; }
    u8 req[] = {SID_DIAGNOSTIC_SESSION_CONTROL, (u8)s};
    send_and_recv(req, 2);
}

static void cmd_security(void)
{
    /* requestSeed */
    u8 req1[] = {SID_SECURITY_ACCESS, SUB_REQUEST_SEED};
    printf("  --- RequestSeed ---\n");
    send_and_recv(req1, 2);
}

static void cmd_sendkey(const char *arg)
{
    u8 key[4];
    u16 klen = parse_hex(arg, key, 4);
    if (klen < 4) { printf("  Usage: sendkey <4 hex bytes>\n"); return; }
    u8 req[6] = {SID_SECURITY_ACCESS, SUB_SEND_KEY, 0};
    memcpy(&req[2], key, 4);
    printf("  --- SendKey ---\n");
    send_and_recv(req, 6);
}

static void cmd_read(const char *arg)
{
    u16 did = parse_did(arg);
    if (did == 0) { printf("  Usage: read <did_hex>\n"); return; }
    u8 req[3] = {SID_READ_DATA_BY_IDENTIFIER, (did >> 8) & 0xFF, did & 0xFF};
    send_and_recv(req, 3);
}

static void cmd_write(const char *arg)
{
    char did_str[8], data_str[256];
    if (sscanf(arg, "%7s %255[^\n]", did_str, data_str) < 2) {
        printf("  Usage: write <did_hex> <hex_bytes...>\n"); return;
    }
    u16 did = parse_did(did_str);
    u8 data[128];
    u16 dlen = parse_hex(data_str, data, sizeof(data));
    u8 req[3 + 128];
    req[0] = SID_WRITE_DATA_BY_IDENTIFIER;
    req[1] = (did >> 8) & 0xFF;
    req[2] = did & 0xFF;
    if (dlen > 128) dlen = 128;
    memcpy(&req[3], data, dlen);
    send_and_recv(req, 3 + dlen);
}

static void cmd_dtc(const char *arg)
{
    unsigned int mask = 0x08;
    if (arg && *arg && sscanf(arg, "%x", &mask) != 1) { printf("  Usage: dtc [status_mask_hex]\n"); return; }
    printf("  --- DTC Count (mask=0x%02X) ---\n", mask);
    u8 req1[] = {SID_READ_DTC_INFORMATION, SUB_REPORT_NUMBER_OF_DTC_BY_STATUS_MASK, (u8)mask};
    send_and_recv(req1, 3);

    printf("  --- DTC List ---\n");
    u8 req2[] = {SID_READ_DTC_INFORMATION, SUB_REPORT_DTC_BY_STATUS_MASK, (u8)mask};
    send_and_recv(req2, 3);
}

static void cmd_clear(void)
{
    u8 req[] = {SID_CLEAR_DIAGNOSTIC_INFORMATION, 0xFF, 0xFF, 0xFF};
    send_and_recv(req, 4);
}

static void cmd_routine(const char *arg)
{
    u16 rid = parse_did(arg);
    if (rid == 0) { printf("  Usage: routine <rid_hex>\n"); return; }
    u8 req[] = {SID_ROUTINE_CONTROL, SUB_START_ROUTINE, (rid >> 8) & 0xFF, rid & 0xFF};
    send_and_recv(req, 4);
}

static void cmd_download(const char *arg)
{
    u32 addr = 0, size = 256;
    if (arg && *arg && sscanf(arg, "%x %x", &addr, &size) < 1) {
        printf("  Usage: download [addr_hex] [size_hex]\n"); return;
    }
    u8 req[10] = {
        SID_REQUEST_DOWNLOAD, 0x44,
        (addr >> 24) & 0xFF, (addr >> 16) & 0xFF, (addr >> 8) & 0xFF, addr & 0xFF,
        (size >> 24) & 0xFF, (size >> 16) & 0xFF, (size >> 8) & 0xFF, size & 0xFF
    };
    printf("  --- RequestDownload (addr=0x%08X, size=%u) ---\n", addr, size);
    send_and_recv(req, 10);
}

static void cmd_xfer_data(const char *arg)
{
    unsigned int seq = 1;
    if (arg && *arg) sscanf(arg, "%x", &seq);
    u8 buf[UDS_MAX_MSG_LEN];
    buf[0] = SID_TRANSFER_DATA;
    buf[1] = (u8)seq;
    u16 blk = 20; /* 教学: 固定20字节 */
    memset(&buf[2], 0xAA, blk);
    printf("  --- TransferData (seq=%u, len=%u) ---\n", seq, blk);
    send_and_recv(buf, 2 + blk);
}

static void cmd_xfer_exit(void)
{
    u8 req[] = {SID_REQUEST_TRANSFER_EXIT};
    printf("  --- RequestTransferExit ---\n");
    send_and_recv(req, 1);
}

static void cmd_reset(const char *arg)
{
    unsigned int t = SUB_HARD_RESET;
    if (arg && *arg) {
        if (strcmp(arg, "soft") == 0) t = SUB_SOFT_RESET;
        else if (strcmp(arg, "keyoff") == 0) t = SUB_KEY_OFF_ON_RESET;
    }
    u8 req[] = {SID_ECU_RESET, (u8)t};
    send_and_recv(req, 2);
}

static void cmd_tp(void)
{
    u8 req[] = {SID_TESTER_PRESENT, SUB_ZERO};
    send_and_recv(req, 2);
}

static void cmd_dtc_setting(const char *arg)
{
    u8 sub = SUB_DTC_ON;
    if (arg && strcmp(arg, "off") == 0) sub = SUB_DTC_OFF;
    u8 req[] = {SID_CONTROL_DTC_SETTING, sub};
    send_and_recv(req, 2);
}

static void print_help(void)
{
    printf(
"\n  UDS Shell Commands:\n"
"  ─────────────────────────────────────────────────────────────\n"
"  session <01|02|03>   切换诊断会话 (01=默认 02=编程 03=扩展)\n"
"  security              请求安全种子 (再发 sendkey <4 hex>)\n"
"  sendkey <hex4>        发送安全密钥 (4字节hex)\n"
"  read <did>            读取DID (如 read f190)\n"
"  write <did> <hex>     写入DID (如 write ff01 aabb)\n"
"  dtc [mask]            读取DTC (可选状态掩码, 默认0x08)\n"
"  clear                 清除全部DTC\n"
"  routine <rid>         启动例行程序 (如 routine 0201)\n"
"  download [addr] [sz]  请求下载 (如 download 40000 100)\n"
"  xfer [seq]            传输数据块 (默认seq=1)\n"
"  xexit                 传输终止\n"
"  tp                    TesterPresent 保活\n"
"  reset [hard|soft|keyoff]  ECU复位\n"
"  dtcset [on|off]       暂停/恢复DTC记录\n"
"  help                  显示此帮助\n"
"  quit                  退出\n"
"  ─────────────────────────────────────────────────────────────\n"
    );
}

/* ── 主循环 ──────────────────────────────── */
int main(int argc, char **argv)
{
    const char *ip = (argc > 1) ? argv[1] : "127.0.0.1";
    int port = (argc > 2) ? atoi(argv[2]) : UDS_SERVER_PORT;

    printf("### UDS Interactive Diagnostic Shell ###\n");
    signal(SIGPIPE, SIG_IGN);
    printf("    Connecting to ECU at %s:%d ...\n", ip, port);

    if (connect_ecu(ip, port) < 0) return 1;
    printf("    Connected. Type 'help' for commands.\n\n");

    char line[512];
    for (;;) {
        printf("uds> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;

        /* 移除末尾换行 */
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = '\0';

        char cmd[32] = {0}, rest[480] = {0};
        int n = sscanf(line, "%31s %479[^\n]", cmd, rest);
        if (n < 1) continue;

        if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0) {
            break;
        } else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
            print_help();
        } else if (strcmp(cmd, "session") == 0) {
            cmd_session(rest);
        } else if (strcmp(cmd, "security") == 0) {
            cmd_security();
        } else if (strcmp(cmd, "sendkey") == 0) {
            cmd_sendkey(rest);
        } else if (strcmp(cmd, "read") == 0) {
            cmd_read(rest);
        } else if (strcmp(cmd, "write") == 0) {
            cmd_write(rest);
        } else if (strcmp(cmd, "dtc") == 0) {
            cmd_dtc(rest);
        } else if (strcmp(cmd, "clear") == 0) {
            cmd_clear();
        } else if (strcmp(cmd, "routine") == 0) {
            cmd_routine(rest);
        } else if (strcmp(cmd, "download") == 0) {
            cmd_download(rest);
        } else if (strcmp(cmd, "xfer") == 0) {
            cmd_xfer_data(rest);
        } else if (strcmp(cmd, "xexit") == 0) {
            cmd_xfer_exit();
        } else if (strcmp(cmd, "tp") == 0) {
            cmd_tp();
        } else if (strcmp(cmd, "reset") == 0) {
            cmd_reset(rest);
        } else if (strcmp(cmd, "dtcset") == 0) {
            cmd_dtc_setting(rest);
        } else {
            printf("  Unknown command: %s (type 'help')\n", cmd);
        }
    }

    printf("Disconnecting...\n");
    close(sock_fd);
    return 0;
}
