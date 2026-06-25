/* uds_client.c — uds-lite 诊断仪端自动化工作流
 * 连接ECU服务器，执行完整的诊断流程。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "uds_common.h"
#include "uds_msg.h"

static int sock_fd = -1;
static u8 recv_buf[UDS_MAX_MSG_LEN];

/* ── 网络层 ──────────────────────────────── */
static int connect_to_ecu(const char *ip, int port)
{
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) { perror("socket"); return -1; }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        perror("inet_pton"); close(sock_fd); return -1;
    }
    if (connect(sock_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect"); close(sock_fd); return -1;
    }

    struct timeval tv = {UDS_RX_TIMEOUT_SEC, 0};
    setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    return 0;
}

static ssize_t send_request(const u8 *data, u16 len)
{
    return send(sock_fd, data, len, 0);
}

static ssize_t recv_response(u8 *buf, u16 max_len)
{
    return recv(sock_fd, buf, max_len, 0);
}

/* ── 响应解析与显示 ──────────────────────── */
static const char *nrc_name(u8 nrc)
{
    switch (nrc) {
        case NRC_SERVICE_NOT_SUPPORTED:        return "ServiceNotSupported";
        case NRC_SUBFUNCTION_NOT_SUPPORTED:    return "SubFunctionNotSupported";
        case NRC_INCORRECT_MESSAGE_LENGTH:     return "IncorrectMessageLength";
        case NRC_CONDITIONS_NOT_CORRECT:       return "ConditionsNotCorrect";
        case NRC_REQUEST_SEQUENCE_ERROR:       return "RequestSequenceError";
        case NRC_REQUEST_OUT_OF_RANGE:         return "RequestOutOfRange";
        case NRC_SECURITY_ACCESS_DENIED:       return "SecurityAccessDenied";
        case NRC_INVALID_KEY:                  return "InvalidKey";
        case NRC_WRONG_BLOCK_SEQ_COUNTER:      return "WrongBlockSequenceCounter";
        case NRC_RESPONSE_PENDING:             return "ResponsePending";
        case NRC_SERVICE_NOT_IN_ACTIVE_SESSION: return "ServiceNotSupportedInActiveSession";
        default:                               return "Unknown";
    }
}

static void dump_hex(const char *label, const u8 *data, u16 len)
{
    printf("  %s [%u bytes]: ", label, len);
    for (u16 i = 0; i < len; i++) printf("%02X ", data[i]);
    printf("\n");
}

static u8 check_negative_response(const u8 *raw, u16 raw_len)
{
    if (raw_len >= 3 && raw[0] == NEGATIVE_RESPONSE_SID) {
        printf("  !! NEGATIVE RESPONSE: SID=0x%02X NRC=0x%02X (%s)\n",
               raw[1], raw[2], nrc_name(raw[2]));
        return TRUE;
    }
    return FALSE;
}

static u8 check_positive(const u8 *raw, u16 len, u8 expected_sid)
{
    if (len >= 1 && raw[0] == (expected_sid | SID_POSITIVE_RESPONSE_MASK)) {
        return TRUE;
    }
    return FALSE;
}

/* ── 诊断工作流 ──────────────────────────── */

static void do_session_control(u8 session)
{
    
    u8 req[] = {SID_DIAGNOSTIC_SESSION_CONTROL, session};
    send_request(req, 2);
    ssize_t n = recv_response(recv_buf, sizeof(recv_buf));
    if (n <= 0) { printf("  No response\n"); return; }
    dump_hex("Rx", recv_buf, n);
    if (check_negative_response(recv_buf, n)) return;
    if (check_positive(recv_buf, n, SID_DIAGNOSTIC_SESSION_CONTROL)) {
        printf("  => Session changed to 0x%02X\n", session);
    }
}

static void do_tester_present(void)
{

    u8 req[] = {SID_TESTER_PRESENT, SUB_ZERO};
    send_request(req, 2);
    ssize_t n = recv_response(recv_buf, sizeof(recv_buf));
    if (n <= 0) { printf("  No response\n"); return; }
    dump_hex("Rx", recv_buf, n);
}

static void do_security_access(void)
{


    /* requestSeed */
    u8 req1[] = {SID_SECURITY_ACCESS, SUB_REQUEST_SEED};
    send_request(req1, 2);
    ssize_t n1 = recv_response(recv_buf, sizeof(recv_buf));
    if (n1 <= 0 || check_negative_response(recv_buf, n1)) return;
    dump_hex("Seed Response", recv_buf, n1);

    /* extract seed: 响应格式 = 正响应SID + 子功能0x01 + 4字节seed */
    u8 seed[4] = {0};
    if (n1 >= 6) memcpy(seed, &recv_buf[2], 4);
    printf("  Seed: %02X %02X %02X %02X\n", seed[0], seed[1], seed[2], seed[3]);

    /* TEACHING: 生产代码中密钥算法可能很复杂。这里用XOR 0x5A */
    u8 key[4];
    for (int i = 0; i < 4; i++) key[i] = seed[i] ^ 0x5A;
    printf("  Computed Key: %02X %02X %02X %02X\n", key[0], key[1], key[2], key[3]);

    /* sendKey */
    u8 req2[6] = {SID_SECURITY_ACCESS, SUB_SEND_KEY, 0};
    memcpy(&req2[2], key, 4);
    send_request(req2, 6);
    ssize_t n2 = recv_response(recv_buf, sizeof(recv_buf));
    if (n2 <= 0) return;
    dump_hex("Key Response", recv_buf, n2);
    if (check_negative_response(recv_buf, n2)) return;
    if (check_positive(recv_buf, n2, SID_SECURITY_ACCESS)) {
        printf("  => Security Level unlocked to Level 1\n");
    }
}

static void do_read_did(u16 did, const char *label)
{
    printf("\n--- ReadDID 0x%04X (%s) ---\n", did, label);
    u8 req[3] = {SID_READ_DATA_BY_IDENTIFIER, (did >> 8) & 0xFF, did & 0xFF};
    send_request(req, 3);
    ssize_t n = recv_response(recv_buf, sizeof(recv_buf));
    if (n <= 0 || check_negative_response(recv_buf, n)) return;
    dump_hex("Rx", recv_buf, n);
}

static void do_read_dtc(void)
{


    /* 子功能0x01: 报告数量 */
    u8 req_count[] = {SID_READ_DTC_INFORMATION,
                      SUB_REPORT_NUMBER_OF_DTC_BY_STATUS_MASK,
                      DTC_STATUS_CONFIRMED_DTC | DTC_STATUS_TEST_FAILED};
    send_request(req_count, 3);
    ssize_t n1 = recv_response(recv_buf, sizeof(recv_buf));
    if (n1 <= 0 || check_negative_response(recv_buf, n1)) return;
    dump_hex("DTC Count", recv_buf, n1);
    if (n1 >= 6) {
        u16 count = uds_read_u16_be(&recv_buf[4]);
        printf("  => %u confirmed+testFailed DTC(s)\n", count);
    }

    /* 子功能0x02: 报告列表 */
    u8 req_list[] = {SID_READ_DTC_INFORMATION,
                     SUB_REPORT_DTC_BY_STATUS_MASK,
                     DTC_STATUS_CONFIRMED_DTC | DTC_STATUS_TEST_FAILED};
    send_request(req_list, 3);
    ssize_t n2 = recv_response(recv_buf, sizeof(recv_buf));
    if (n2 <= 0 || check_negative_response(recv_buf, n2)) return;
    dump_hex("DTC List", recv_buf, n2);
}

static void do_routine_control(u16 rid)
{

    u8 req[] = {SID_ROUTINE_CONTROL, SUB_START_ROUTINE, (rid >> 8) & 0xFF, rid & 0xFF};
    send_request(req, 4);
    ssize_t n = recv_response(recv_buf, sizeof(recv_buf));
    if (n <= 0 || check_negative_response(recv_buf, n)) return;
    dump_hex("Rx", recv_buf, n);
    if (n >= 5) printf("  => RoutineStatus: 0x%02X\n", recv_buf[4]);
}

static void do_download(void)
{


    /* 0x34 请求下载: 地址0x00040000, 长度256字节 */
    u8 download_req[] = {
        SID_REQUEST_DOWNLOAD,
        0x00,  /* dataFormatIdentifier = 0x00 (无压缩/加密) */
        0x44,  /* addrWidth=4, sizeWidth=4 */
        0x00, 0x04, 0x00, 0x00,  /* memoryAddress = 0x00040000 */
        0x00, 0x00, 0x01, 0x00   /* memorySize = 256 */
    };
    send_request(download_req, sizeof(download_req));
    ssize_t n1 = recv_response(recv_buf, sizeof(recv_buf));
    if (n1 <= 0 || check_negative_response(recv_buf, n1)) return;
    dump_hex("RequestDownload Response", recv_buf, n1);

    u16 max_blk = (n1 >= 5) ? uds_read_u16_be(&recv_buf[3]) : 200;
    if (max_blk > 200) max_blk = 200;  /* 防止缓冲区溢出，教学代码限定200字节 */
    printf("  MaxBlockSize = %u bytes\n", max_blk);

    /* 0x36 传输数据块 */
    u8 blk_data[210];
    blk_data[0] = SID_TRANSFER_DATA;
    blk_data[1] = 1;  /* blockSequenceCounter = 1 */
    memset(&blk_data[2], 0xCC, max_blk); /* 填充假固件数据 */

    printf("  Sending block 1 (%u bytes)...\n", max_blk);
    send_request(blk_data, 2 + max_blk);
    ssize_t n2 = recv_response(recv_buf, sizeof(recv_buf));
    if (n2 <= 0 || check_negative_response(recv_buf, n2)) return;
    dump_hex("TransferData Response", recv_buf, n2);

    /* 0x37 传输终止 */
    u8 exit_req[] = {SID_REQUEST_TRANSFER_EXIT};
    send_request(exit_req, 1);
    ssize_t n3 = recv_response(recv_buf, sizeof(recv_buf));
    if (n3 <= 0 || check_negative_response(recv_buf, n3)) return;
    dump_hex("TransferExit Response", recv_buf, n3);
    printf("  => Download complete.\n");
}

static void do_clear_dtc(void)
{

    u8 req[] = {SID_CLEAR_DIAGNOSTIC_INFORMATION, 0xFF, 0xFF, 0xFF};
    send_request(req, 4);
    ssize_t n = recv_response(recv_buf, sizeof(recv_buf));
    if (n <= 0 || check_negative_response(recv_buf, n)) return;
    dump_hex("Rx", recv_buf, n);
}

/* ── 主入口 ──────────────────────────────── */
int main(int argc, char **argv)
{
    const char *ip = (argc > 1) ? argv[1] : "127.0.0.1";
    int port = (argc > 2) ? atoi(argv[2]) : UDS_SERVER_PORT;

    printf("### UDS Client (Tester) connecting to %s:%d ###\n\n", ip, port);

    if (connect_to_ecu(ip, port) < 0) return 1;

    /* ── 完整的诊断工作流 ─────────────────── */
    printf("\n=== Step 1: DiagnosticSessionControl (enter session 0x03) ===\n");
    do_session_control(SESSION_EXTENDED);
    printf("\n=== Step 2: TesterPresent (keep-alive) ===\n");
    do_tester_present();
    printf("\n=== Step 3: SecurityAccess (seed/key to unlock level 1) ===\n");
    do_security_access();

    /* 读几个DID */
    do_read_did(0x010C, "Engine RPM");
    do_read_did(0x0105, "Coolant Temp");
    do_read_did(0xF190, "VIN");
    do_read_did(0xF180, "SW Version");

    /* 读DTC */
    printf("\n=== Step 4: ReadDTC (report by status mask) ===\n");
    do_read_dtc();

    /* 例行控制 */
    printf("\n=== Step 5: RoutineControl (start routine 0x0201) ===\n");
    do_routine_control(0x0201);

    /* 下载 (需要编程会话, 先切换) */
    printf("\n=== Step 6: Programming session + Download ===\n");
    do_session_control(SESSION_PROGRAMMING);
    do_download();

    /* 切回扩展，清除DTC */
    printf("\n=== Step 7: Clear DTC ===\n");
    do_session_control(SESSION_EXTENDED);
    do_security_access();
    do_clear_dtc();

    /* 复位并退出 */
    printf("\n=== Step 8: ECU Reset (soft) ===\n");
    {
        u8 req[] = {SID_ECU_RESET, SUB_SOFT_RESET};
        send_request(req, 2);
        ssize_t n = recv_response(recv_buf, sizeof(recv_buf));
        if (n > 0) dump_hex("Rx", recv_buf, n);
    }

    printf("\n### Diagnostic workflow complete ###\n");
    close(sock_fd);
    return 0;
}
