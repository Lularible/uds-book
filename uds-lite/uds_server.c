/* uds_server.c -- uds-lite ECU-side diagnostic server
 * Simulates an ECU, listens on TCP port 13400 and handles UDS requests.
 *
 * Supported services:
 *   0x10 diagnostic session control   0x3E tester present
 *   0x11 ECU reset                    0x27 security access
 *   0x22 read data by identifier      0x2E write data by identifier
 *   0x19 read DTC information         0x14 clear diagnostic information
 *   0x31 routine control              0x34/36/37 download
 *   0x85 control DTC setting
 *
 * TEACHING: This implementation is ~500 lines; a production-grade
 * AUTOSAR DCM is 15,000+ lines.
 * Simplifications: no buffer management (single connection processed
 * synchronously), no state machine resumability, no S3 timeout (production
 * uses select() or a hardware timer), and a hardcoded XOR seed/key algorithm.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include "uds_common.h"
#include "uds_msg.h"

/* -- Global ECU state ---------------------------------- */
static u8  g_session      = SESSION_DEFAULT;
static u8  g_security     = SECURITY_LOCKED;
static u8  g_dtc_setting  = SUB_DTC_ON;
static u8  g_seed[4]      = {0};
static u8  g_seed_pending = FALSE;
static u8  g_dtc_count    = 0;
static TransferState g_xfer = {FALSE, 0, 0, 0, 0, 0};
static RoutineState g_routine = {0, FALSE, 0, 0};

/* -- DID table (teaching simplification: statically defined) -- */
#define DID_COUNT 8
static DidRecord g_did_table[DID_COUNT];

static void init_did_table(void)
{
    /* DID 0x010C: engine speed, 0.25 rpm/bit, 2 bytes */
    DidRecord *d = &g_did_table[0];
    d->did = 0x010C; d->writable = FALSE;
    d->session_required = SESSION_DEFAULT; d->security_required = SECURITY_LOCKED;
    d->data[0] = 0x0B; d->data[1] = 0xB8; d->data_len = 2; /* 3000/4=750*4=3000rpm --> 0x0BB8 = 3000 */

    /* DID 0x0105: coolant temperature, -40C offset, 1C/bit, 1 byte */
    d = &g_did_table[1];
    d->did = 0x0105; d->writable = FALSE;
    d->session_required = SESSION_DEFAULT; d->security_required = SECURITY_LOCKED;
    d->data[0] = 0x84; /* 92C -> 92+40=132=0x84 */ d->data_len = 1;

    /* DID 0xF190: VIN "W0L00000123456789", 17 bytes */
    d = &g_did_table[2];
    d->did = 0xF190; d->writable = TRUE;
    d->session_required = SESSION_EXTENDED; d->security_required = SECURITY_LEVEL_1;
    memcpy(d->data, "W0L00000123456789", 17); d->data_len = 17;

    /* DID 0x010D: vehicle speed, 1 km/h/bit */
    d = &g_did_table[3];
    d->did = 0x010D; d->writable = FALSE;
    d->session_required = SESSION_DEFAULT; d->security_required = SECURITY_LOCKED;
    d->data[0] = 87; d->data_len = 1;

    /* DID 0x0110: MAF air flow rate, 0.01 g/s/bit */
    d = &g_did_table[4];
    d->did = 0x0110; d->writable = FALSE;
    d->session_required = SESSION_DEFAULT; d->security_required = SECURITY_LOCKED;
    d->data[0] = 0x00; d->data[1] = 0xAA; d->data_len = 2; /* 170*0.01=1.70g/s -> 0x00AA */

    /* DID 0xF180: ECU software version "SW-V1.0.0" */
    d = &g_did_table[5];
    d->did = 0xF180; d->writable = FALSE;
    d->session_required = SESSION_DEFAULT; d->security_required = SECURITY_LOCKED;
    memcpy(d->data, "SW-V1.0.0", 9); d->data_len = 9;

    /* DID 0xFF01: diagnostic calibration ID (teaching-only, writable) */
    d = &g_did_table[6];
    d->did = 0xFF01; d->writable = TRUE;
    d->session_required = SESSION_EXTENDED; d->security_required = SECURITY_LEVEL_1;
    d->data[0] = 0x00; d->data[1] = 0x00; d->data_len = 2;

    /* DID 0x0150: short-term fuel trim, 1 byte, -100% to +99.2% */
    d = &g_did_table[7];
    d->did = 0x0150; d->writable = FALSE;
    d->session_required = SESSION_DEFAULT; d->security_required = SECURITY_LOCKED;
    d->data[0] = 0x83; d->data_len = 1; /* 0x83=131, (131-128)*100/128=+2.3% */
}

static DidRecord *find_did(u16 did)
{
    for (int i = 0; i < DID_COUNT; i++) {
        if (g_did_table[i].did == did) return &g_did_table[i];
    }
    return NULL;
}

/* -- DTC storage (teaching simplification: two fixed DTCs) -- */
static DtcRecord g_dtc_table[UDS_MAX_DTC_COUNT];

static void init_dtc_table(void)
{
    /* DTC1: P0102 MAF sensor circuit low voltage - confirmed + testFailed */
    g_dtc_table[0].code[0] = 0x00; g_dtc_table[0].code[1] = 0x01; g_dtc_table[0].code[2] = 0x02;
    g_dtc_table[0].status = DTC_STATUS_TEST_FAILED | DTC_STATUS_CONFIRMED_DTC;
    g_dtc_table[0].snapshot_count = 2;
    g_dtc_table[0].snapshot_dids[0] = 0x010C; /* RPM */
    g_dtc_table[0].snapshot_dids[1] = 0x010D; /* Speed */

    /* DTC2: C0421 ABS pump motor fault - pending + testFailed */
    g_dtc_table[1].code[0] = 0x40; g_dtc_table[1].code[1] = 0x02; g_dtc_table[1].code[2] = 0x01;
    g_dtc_table[1].status = DTC_STATUS_TEST_FAILED | DTC_STATUS_PENDING_DTC;
    g_dtc_table[1].snapshot_count = 1;
    g_dtc_table[1].snapshot_dids[0] = 0x0150; /* STFT */

    g_dtc_count = 2;
}

/* -- SecurityAccess: teaching-grade seed/key algorithm -- */
static void compute_seed(u8 seed[4])
{
    /* TEACHING: production code uses a hardware TRNG. Fixed value here. */
    seed[0] = 0xA3; seed[1] = 0xD4; seed[2] = 0x5F; seed[3] = 0x12;
}

static u8 verify_key(const u8 *key, u8 key_len)
{
    /* TEACHING: production code uses AES/HMAC. XOR with fixed key = 0x5A here. */
    if (key_len < 4) return FALSE;
    u8 expected[4];
    for (int i = 0; i < 4; i++) expected[i] = g_seed[i] ^ 0x5A;
    return (memcmp(key, expected, 4) == 0);
}

/* -- SID dispatch -------------------------------------- */

static void handle_session_control(const u8 *req, u16 req_len, u8 *resp, u16 *resp_len)
{
    if (req_len < 2) { *resp_len = uds_build_negative_response(resp, SID_DIAGNOSTIC_SESSION_CONTROL, NRC_INCORRECT_MESSAGE_LENGTH); return; }
    u8 sub = req[1] & 0x7F;
    u8 suppress = (req[1] & SUPPRESS_POS_RSP_MASK) ? 1 : 0;

    if (sub != SESSION_DEFAULT && sub != SESSION_PROGRAMMING && sub != SESSION_EXTENDED) {
        *resp_len = uds_build_negative_response(resp, SID_DIAGNOSTIC_SESSION_CONTROL, NRC_SUBFUNCTION_NOT_SUPPORTED); return;
    }

    g_session = sub;
    g_security = SECURITY_LOCKED;
    g_seed_pending = FALSE;

    if (suppress) { *resp_len = 0; return; }

    u8 extra[] = {sub, (P2_SERVER_DEFAULT >> 8), P2_SERVER_DEFAULT & 0xFF,
                        (P2_STAR_SERVER_DEFAULT >> 8) & 0xFF, P2_STAR_SERVER_DEFAULT & 0xFF};
    *resp_len = uds_build_positive_response(resp, SID_DIAGNOSTIC_SESSION_CONTROL, extra, 5);
}

static void handle_tester_present(const u8 *req, u16 req_len, u8 *resp, u16 *resp_len)
{
    if (req_len < 2) { *resp_len = uds_build_negative_response(resp, SID_TESTER_PRESENT, NRC_INCORRECT_MESSAGE_LENGTH); return; }
    u8 suppress = (req[1] & SUPPRESS_POS_RSP_MASK) ? 1 : 0;
    if (suppress) { *resp_len = 0; return; }
    resp[0] = SID_TESTER_PRESENT | SID_POSITIVE_RESPONSE_MASK;
    resp[1] = SUB_ZERO;
    *resp_len = 2;
}

static void handle_ecu_reset(const u8 *req, u16 req_len, u8 *resp, u16 *resp_len)
{
    if (req_len < 2) { *resp_len = uds_build_negative_response(resp, SID_ECU_RESET, NRC_INCORRECT_MESSAGE_LENGTH); return; }
    u8 sub = req[1] & 0x7F;
    if (sub < SUB_HARD_RESET || sub > SUB_SOFT_RESET) {
        *resp_len = uds_build_negative_response(resp, SID_ECU_RESET, NRC_SUBFUNCTION_NOT_SUPPORTED); return;
    }
    /* Positive response is sent before performing the reset */
    u8 extra[] = {sub};
    *resp_len = uds_build_positive_response(resp, SID_ECU_RESET, extra, 1);
}

static void handle_security_access(const u8 *req, u16 req_len, u8 *resp, u16 *resp_len)
{
    if (req_len < 2) { *resp_len = uds_build_negative_response(resp, SID_SECURITY_ACCESS, NRC_INCORRECT_MESSAGE_LENGTH); return; }
    u8 sub = req[1] & 0x7F;

    if (sub == SUB_REQUEST_SEED) {
        /* Odd sub-function -> request seed */
        compute_seed(g_seed);
        g_seed_pending = TRUE;
        u8 extra[5] = {SUB_REQUEST_SEED};
        memcpy(&extra[1], g_seed, 4);
        *resp_len = uds_build_positive_response(resp, SID_SECURITY_ACCESS, extra, 5);
    } else if (sub == SUB_SEND_KEY && g_seed_pending) {
        /* Even sub-function -> send key */
        u8 key[4];
        if (req_len < 6) { *resp_len = uds_build_negative_response(resp, SID_SECURITY_ACCESS, NRC_INCORRECT_MESSAGE_LENGTH); return; }
        memcpy(key, &req[2], 4);
        if (verify_key(key, 4)) {
            g_security = SECURITY_LEVEL_1;
            g_seed_pending = FALSE;
            u8 extra[] = {SUB_SEND_KEY};
            *resp_len = uds_build_positive_response(resp, SID_SECURITY_ACCESS, extra, 1);
        } else {
            *resp_len = uds_build_negative_response(resp, SID_SECURITY_ACCESS, NRC_INVALID_KEY);
        }
    } else {
        *resp_len = uds_build_negative_response(resp, SID_SECURITY_ACCESS, NRC_REQUEST_SEQUENCE_ERROR);
    }
}

static void handle_read_did(const u8 *req, u16 req_len, u8 *resp, u16 *resp_len)
{
    if (req_len < 3 || (req_len - 1) % 2 != 0) {
        *resp_len = uds_build_negative_response(resp, SID_READ_DATA_BY_IDENTIFIER, NRC_INCORRECT_MESSAGE_LENGTH); return;
    }

    u8 out[UDS_MAX_MSG_LEN];
    u16 pos = 0;
    out[pos++] = SID_READ_DATA_BY_IDENTIFIER | SID_POSITIVE_RESPONSE_MASK;

    u16 idx = 1;
    while (idx + 1 < req_len) {
        u16 did = uds_read_u16_be(&req[idx]);
        DidRecord *d = find_did(did);
        if (!d) {
            *resp_len = uds_build_negative_response(resp, SID_READ_DATA_BY_IDENTIFIER, NRC_REQUEST_OUT_OF_RANGE); return;
        }
        if (g_session < d->session_required) {
            *resp_len = uds_build_negative_response(resp, SID_READ_DATA_BY_IDENTIFIER, NRC_SERVICE_NOT_IN_ACTIVE_SESSION); return;
        }
        if (g_security < d->security_required) {
            *resp_len = uds_build_negative_response(resp, SID_READ_DATA_BY_IDENTIFIER, NRC_SECURITY_ACCESS_DENIED); return;
        }
        if (pos + 2 + d->data_len > UDS_MAX_MSG_LEN) {
            *resp_len = uds_build_negative_response(resp, SID_READ_DATA_BY_IDENTIFIER, NRC_RESPONSE_TOO_LONG); return;
        }
        out[pos++] = (did >> 8) & 0xFF;
        out[pos++] = did & 0xFF;
        memcpy(&out[pos], d->data, d->data_len);
        pos += d->data_len;
        idx += 2;
    }
    memcpy(resp, out, pos);
    *resp_len = pos;
}

static void handle_write_did(const u8 *req, u16 req_len, u8 *resp, u16 *resp_len)
{
    if (req_len < 4) { *resp_len = uds_build_negative_response(resp, SID_WRITE_DATA_BY_IDENTIFIER, NRC_INCORRECT_MESSAGE_LENGTH); return; }
    u16 did = uds_read_u16_be(&req[1]);
    DidRecord *d = find_did(did);
    if (!d) { *resp_len = uds_build_negative_response(resp, SID_WRITE_DATA_BY_IDENTIFIER, NRC_REQUEST_OUT_OF_RANGE); return; }
    if (!d->writable) { *resp_len = uds_build_negative_response(resp, SID_WRITE_DATA_BY_IDENTIFIER, NRC_REQUEST_OUT_OF_RANGE); return; }
    if (g_session < d->session_required) { *resp_len = uds_build_negative_response(resp, SID_WRITE_DATA_BY_IDENTIFIER, NRC_SERVICE_NOT_IN_ACTIVE_SESSION); return; }
    if (g_security < d->security_required) { *resp_len = uds_build_negative_response(resp, SID_WRITE_DATA_BY_IDENTIFIER, NRC_SECURITY_ACCESS_DENIED); return; }

    u16 data_len = req_len - 3;
    if (data_len != d->data_len) { *resp_len = uds_build_negative_response(resp, SID_WRITE_DATA_BY_IDENTIFIER, NRC_INCORRECT_MESSAGE_LENGTH); return; }
    memcpy(d->data, &req[3], data_len);

    u8 extra[] = {(did >> 8) & 0xFF, did & 0xFF};
    *resp_len = uds_build_positive_response(resp, SID_WRITE_DATA_BY_IDENTIFIER, extra, 2);
}

static void handle_read_dtc(const u8 *req, u16 req_len, u8 *resp, u16 *resp_len)
{
    if (req_len < 3) { *resp_len = uds_build_negative_response(resp, SID_READ_DTC_INFORMATION, NRC_INCORRECT_MESSAGE_LENGTH); return; }
    u8 sub = req[1] & 0x7F;
    u8 status_mask = req[2];

    if (sub == SUB_REPORT_NUMBER_OF_DTC_BY_STATUS_MASK) {
        u8 count = 0;
        for (int i = 0; i < g_dtc_count; i++) {
            if (g_dtc_table[i].status & status_mask) count++;
        }
        u8 extra[] = {SUB_REPORT_NUMBER_OF_DTC_BY_STATUS_MASK, status_mask, 0x01, (count >> 8) & 0xFF, count & 0xFF};
        *resp_len = uds_build_positive_response(resp, SID_READ_DTC_INFORMATION, extra, sizeof(extra));
    } else if (sub == SUB_REPORT_DTC_BY_STATUS_MASK) {
        u8 extra[UDS_MAX_MSG_LEN];
        u16 pos = 0;
        extra[pos++] = SUB_REPORT_DTC_BY_STATUS_MASK;
        extra[pos++] = status_mask;
        for (int i = 0; i < g_dtc_count; i++) {
            if (g_dtc_table[i].status & status_mask) {
                memcpy(&extra[pos], g_dtc_table[i].code, 3); pos += 3;
                extra[pos++] = g_dtc_table[i].status;
            }
        }
        *resp_len = uds_build_positive_response(resp, SID_READ_DTC_INFORMATION, extra, pos);
    } else {
        *resp_len = uds_build_negative_response(resp, SID_READ_DTC_INFORMATION, NRC_SUBFUNCTION_NOT_SUPPORTED);
    }
}

static void handle_clear_dtc(const u8 *req, u16 req_len, u8 *resp, u16 *resp_len)
{
    (void)req;
    if (req_len < 4) { *resp_len = uds_build_negative_response(resp, SID_CLEAR_DIAGNOSTIC_INFORMATION, NRC_INCORRECT_MESSAGE_LENGTH); return; }
    /* groupOfDTC = 0xFFFFFF clears everything */
    g_dtc_count = 0;
    resp[0] = SID_CLEAR_DIAGNOSTIC_INFORMATION | SID_POSITIVE_RESPONSE_MASK;
    *resp_len = 1;
}

static void handle_routine_control(const u8 *req, u16 req_len, u8 *resp, u16 *resp_len)
{
    if (req_len < 4) { *resp_len = uds_build_negative_response(resp, SID_ROUTINE_CONTROL, NRC_INCORRECT_MESSAGE_LENGTH); return; }
    if (g_session < SESSION_EXTENDED) { *resp_len = uds_build_negative_response(resp, SID_ROUTINE_CONTROL, NRC_SERVICE_NOT_IN_ACTIVE_SESSION); return; }

    u8 sub = req[1] & 0x7F;
    u16 rid = uds_read_u16_be(&req[2]);

    if (sub == SUB_START_ROUTINE) {
        g_routine.rid = rid;
        g_routine.activated = TRUE;
        g_routine.current_status = 0x01; /* completed */
        g_routine.progress = 100;
        u8 extra[4] = {SUB_START_ROUTINE, (rid >> 8) & 0xFF, rid & 0xFF, 0x01};
        *resp_len = uds_build_positive_response(resp, SID_ROUTINE_CONTROL, extra, 4);
    } else if (sub == SUB_STOP_ROUTINE) {
        g_routine.activated = FALSE;
        u8 extra[4] = {SUB_STOP_ROUTINE, (rid >> 8) & 0xFF, rid & 0xFF, 0x04};
        *resp_len = uds_build_positive_response(resp, SID_ROUTINE_CONTROL, extra, 4);
    } else if (sub == SUB_REQUEST_ROUTINE_RESULTS) {
        u8 extra[4] = {SUB_REQUEST_ROUTINE_RESULTS, (rid >> 8) & 0xFF, rid & 0xFF, g_routine.current_status};
        *resp_len = uds_build_positive_response(resp, SID_ROUTINE_CONTROL, extra, 4);
    } else {
        *resp_len = uds_build_negative_response(resp, SID_ROUTINE_CONTROL, NRC_SUBFUNCTION_NOT_SUPPORTED);
    }
}

static void handle_request_download(const u8 *req, u16 req_len, u8 *resp, u16 *resp_len)
{
    if (req_len < 4) { *resp_len = uds_build_negative_response(resp, SID_REQUEST_DOWNLOAD, NRC_INCORRECT_MESSAGE_LENGTH); return; }
    if (g_session != SESSION_PROGRAMMING) { *resp_len = uds_build_negative_response(resp, SID_REQUEST_DOWNLOAD, NRC_CONDITIONS_NOT_CORRECT); return; }

    u8 addr_len_fmt = req[1];
    u8 addr_w = addr_len_fmt & 0x0F;
    u8 size_w = (addr_len_fmt >> 4) & 0x0F;

    if (req_len < 2 + addr_w + size_w) {
        *resp_len = uds_build_negative_response(resp, SID_REQUEST_DOWNLOAD, NRC_INCORRECT_MESSAGE_LENGTH); return;
    }

    u32 addr = 0, size = 0;
    for (u8 i = 0; i < addr_w; i++) addr = (addr << 8) | req[2 + i];
    for (u8 i = 0; i < size_w; i++) size = (size << 8) | req[2 + addr_w + i];

    g_xfer.active = TRUE;
    g_xfer.direction = 0;
    g_xfer.address = addr;
    g_xfer.remaining_size = size;
    g_xfer.max_block_size = 200;
    g_xfer.expected_seq_counter = 1;

    u8 extra[] = {0x20, (g_xfer.max_block_size >> 8) & 0xFF, g_xfer.max_block_size & 0xFF};
    *resp_len = uds_build_positive_response(resp, SID_REQUEST_DOWNLOAD, extra, 3);
}

static void handle_transfer_data(const u8 *req, u16 req_len, u8 *resp, u16 *resp_len)
{
    if (!g_xfer.active) { *resp_len = uds_build_negative_response(resp, SID_TRANSFER_DATA, NRC_REQUEST_SEQUENCE_ERROR); return; }
    if (req_len < 2) { *resp_len = uds_build_negative_response(resp, SID_TRANSFER_DATA, NRC_INCORRECT_MESSAGE_LENGTH); return; }

    u8 seq = req[1];
    if (seq != g_xfer.expected_seq_counter) {
        *resp_len = uds_build_negative_response(resp, SID_TRANSFER_DATA, NRC_WRONG_BLOCK_SEQ_COUNTER); return;
    }

    u16 block_len = req_len - 2;
    if (block_len > 0 && g_xfer.remaining_size >= block_len) {
        g_xfer.address += block_len;
        g_xfer.remaining_size -= block_len;
    }

    g_xfer.expected_seq_counter++;
    if (g_xfer.expected_seq_counter == 0) g_xfer.expected_seq_counter = 1;

    u8 extra[] = {seq};
    *resp_len = uds_build_positive_response(resp, SID_TRANSFER_DATA, extra, 1);
}

static void handle_transfer_exit(const u8 *req, u16 req_len, u8 *resp, u16 *resp_len)
{
    (void)req; (void)req_len;
    if (!g_xfer.active) { *resp_len = uds_build_negative_response(resp, SID_REQUEST_TRANSFER_EXIT, NRC_REQUEST_SEQUENCE_ERROR); return; }
    g_xfer.active = FALSE;
    *resp_len = uds_build_positive_response(resp, SID_REQUEST_TRANSFER_EXIT, NULL, 0);
}

static void handle_control_dtc_setting(const u8 *req, u16 req_len, u8 *resp, u16 *resp_len)
{
    if (req_len < 2) { *resp_len = uds_build_negative_response(resp, SID_CONTROL_DTC_SETTING, NRC_INCORRECT_MESSAGE_LENGTH); return; }
    u8 sub = req[1] & 0x7F;
    if (sub != SUB_DTC_ON && sub != SUB_DTC_OFF) {
        *resp_len = uds_build_negative_response(resp, SID_CONTROL_DTC_SETTING, NRC_SUBFUNCTION_NOT_SUPPORTED); return;
    }
    g_dtc_setting = sub;
    u8 extra[] = {sub};
    *resp_len = uds_build_positive_response(resp, SID_CONTROL_DTC_SETTING, extra, 1);
}

/* -- Top-level dispatch entry --------------------------- */
static u16 dispatch(const u8 *req, u16 req_len, u8 *resp)
{
    if (req_len < 1) return 0;
    u8 sid = req[0];
    u16 resp_len = 0;

    switch (sid) {
        case SID_DIAGNOSTIC_SESSION_CONTROL: handle_session_control(req, req_len, resp, &resp_len); break;
        case SID_TESTER_PRESENT:             handle_tester_present(req, req_len, resp, &resp_len); break;
        case SID_ECU_RESET:                  handle_ecu_reset(req, req_len, resp, &resp_len); break;
        case SID_SECURITY_ACCESS:            handle_security_access(req, req_len, resp, &resp_len); break;
        case SID_READ_DATA_BY_IDENTIFIER:    handle_read_did(req, req_len, resp, &resp_len); break;
        case SID_WRITE_DATA_BY_IDENTIFIER:   handle_write_did(req, req_len, resp, &resp_len); break;
        case SID_READ_DTC_INFORMATION:       handle_read_dtc(req, req_len, resp, &resp_len); break;
        case SID_CLEAR_DIAGNOSTIC_INFORMATION: handle_clear_dtc(req, req_len, resp, &resp_len); break;
        case SID_ROUTINE_CONTROL:            handle_routine_control(req, req_len, resp, &resp_len); break;
        case SID_REQUEST_DOWNLOAD:           handle_request_download(req, req_len, resp, &resp_len); break;
        case SID_TRANSFER_DATA:              handle_transfer_data(req, req_len, resp, &resp_len); break;
        case SID_REQUEST_TRANSFER_EXIT:      handle_transfer_exit(req, req_len, resp, &resp_len); break;
        case SID_CONTROL_DTC_SETTING:        handle_control_dtc_setting(req, req_len, resp, &resp_len); break;
        default:
            resp_len = uds_build_negative_response(resp, sid, NRC_SERVICE_NOT_SUPPORTED);
            break;
    }
    return resp_len;
}

/* -- Main function -------------------------------------- */
int main(int argc, char **argv)
{
    int port = (argc > 1) ? atoi(argv[1]) : UDS_SERVER_PORT;

    init_did_table();
    init_dtc_table();

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); return 1;
    }
    if (listen(listen_fd, 5) < 0) { perror("listen"); return 1; }

    printf("### UDS Server (ECU) listening on port %d ###\n", port);
    printf("    Session: Default | Security: Locked | DTCs: %d\n", g_dtc_count);
    printf("    Supported DID count: %d\n", DID_COUNT);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) { perror("accept"); continue; }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        printf("[ECU] Tester connected from %s:%d\n", client_ip, ntohs(client_addr.sin_port));

        u8 req_buf[UDS_MAX_MSG_LEN], resp_buf[UDS_MAX_MSG_LEN];

        while (1) {
            ssize_t n = recv(client_fd, req_buf, sizeof(req_buf), 0);
            if (n <= 0) {
                if (n < 0) printf("[ECU] Connection timeout or error\n");
                else printf("[ECU] Tester disconnected\n");
                g_session = SESSION_DEFAULT;
                g_security = SECURITY_LOCKED;
                g_seed_pending = FALSE;
                g_xfer.active = FALSE;
                break;
            }

            u16 resp_len = dispatch(req_buf, (u16)n, resp_buf);
            if (resp_len > 0) {
                send(client_fd, resp_buf, resp_len, 0);
            }
        }
        close(client_fd);
    }

    close(listen_fd);
    return 0;
}
