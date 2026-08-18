/* uds_common.h -- uds-lite shared types and constants
 * TEACHING: This is a minimal teaching-grade implementation. In production
 * these types would be replaced by AUTOSAR Std_Types.h and Dcm_Types.h,
 * which include full memory section control and MISRA annotations.
 */
#ifndef UDS_COMMON_H
#define UDS_COMMON_H

#include <stdint.h>
#include <stddef.h>

/* -- Type aliases ------------------------------------- */
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int16_t  s16;

#ifndef TRUE
#define TRUE  1
#endif
#ifndef FALSE
#define FALSE 0
#endif

/* -- UDS service identifiers (SID) --------------------- */
/* Request SID range: 0x10..0x3E, bit6=0 */
#define SID_DIAGNOSTIC_SESSION_CONTROL   0x10
#define SID_ECU_RESET                    0x11
#define SID_CLEAR_DIAGNOSTIC_INFORMATION 0x14
#define SID_READ_DTC_INFORMATION         0x19
#define SID_READ_DATA_BY_IDENTIFIER      0x22
#define SID_SECURITY_ACCESS              0x27
#define SID_COMMUNICATION_CONTROL        0x28
#define SID_READ_DATA_BY_PERIODIC_ID     0x2A
#define SID_DYNAMICALLY_DEFINE_DID       0x2C
#define SID_WRITE_DATA_BY_IDENTIFIER     0x2E
#define SID_INPUT_OUTPUT_CONTROL_BY_ID   0x2F
#define SID_ROUTINE_CONTROL              0x31
#define SID_REQUEST_DOWNLOAD             0x34
#define SID_REQUEST_UPLOAD               0x35
#define SID_TRANSFER_DATA                0x36
#define SID_REQUEST_TRANSFER_EXIT        0x37
#define SID_WRITE_MEMORY_BY_ADDRESS      0x3D
#define SID_TESTER_PRESENT               0x3E
#define SID_CONTROL_DTC_SETTING          0x85

/* Positive response SID = request SID | 0x40 */
#define SID_POSITIVE_RESPONSE_MASK       0x40
#define NEGATIVE_RESPONSE_SID            0x7F

/* -- UDS standard sub-function codes ------------------- */
/* 0x10 diagnostic session control */
#define SUB_DEFAULT_SESSION              0x01
#define SUB_PROGRAMMING_SESSION          0x02
#define SUB_EXTENDED_SESSION             0x03

/* 0x11 ECU reset */
#define SUB_HARD_RESET                   0x01
#define SUB_KEY_OFF_ON_RESET             0x02
#define SUB_SOFT_RESET                   0x03

/* 0x27 security access */
#define SUB_REQUEST_SEED                 0x01
#define SUB_SEND_KEY                     0x02

/* 0x19 read DTC information */
#define SUB_REPORT_NUMBER_OF_DTC_BY_STATUS_MASK  0x01
#define SUB_REPORT_DTC_BY_STATUS_MASK            0x02

/* 0x31 routine control */
#define SUB_START_ROUTINE                0x01
#define SUB_STOP_ROUTINE                 0x02
#define SUB_REQUEST_ROUTINE_RESULTS      0x03

/* 0x3E tester present */
#define SUB_ZERO                         0x00

/* 0x85 control DTC setting */
#define SUB_DTC_ON                       0x01
#define SUB_DTC_OFF                      0x02

/* suppressPosRsp bit (bit7 of subFunction) */
#define SUPPRESS_POS_RSP_MASK            0x80

/* -- Negative response codes (NRC) --------------------- */
#define NRC_GENERAL_REJECT               0x10
#define NRC_SERVICE_NOT_SUPPORTED        0x11
#define NRC_SUBFUNCTION_NOT_SUPPORTED    0x12
#define NRC_INCORRECT_MESSAGE_LENGTH     0x13
#define NRC_RESPONSE_TOO_LONG            0x14
#define NRC_CONDITIONS_NOT_CORRECT       0x22
#define NRC_REQUEST_SEQUENCE_ERROR       0x24
#define NRC_REQUEST_OUT_OF_RANGE         0x31
#define NRC_SECURITY_ACCESS_DENIED       0x33
#define NRC_INVALID_KEY                  0x35
#define NRC_EXCEEDED_NUMBER_OF_ATTEMPTS  0x36
#define NRC_REQUIRED_TIME_DELAY_NOT_EXP  0x37
#define NRC_WRONG_BLOCK_SEQ_COUNTER      0x73
#define NRC_RESPONSE_PENDING             0x78
#define NRC_SERVICE_NOT_IN_ACTIVE_SESSION 0x7F

/* -- DTC status bit definitions ------------------------ */
#define DTC_STATUS_TEST_FAILED           0x01
#define DTC_STATUS_TEST_FAILED_THIS_OP_CYCLE 0x02
#define DTC_STATUS_PENDING_DTC           0x04
#define DTC_STATUS_CONFIRMED_DTC         0x08
#define DTC_STATUS_TEST_NOT_COMPLETED_SINCE_CLEAR 0x10
#define DTC_STATUS_TEST_FAILED_SINCE_CLEAR 0x20
#define DTC_STATUS_TEST_NOT_COMPLETED_THIS_OP_CYCLE 0x40
#define DTC_STATUS_WARNING_INDICATOR_REQUESTED 0x80

/* -- Session management -------------------------------- */
#define SESSION_DEFAULT                  0x01
#define SESSION_PROGRAMMING              0x02
#define SESSION_EXTENDED                 0x03

#define SECURITY_LOCKED                  0x00
#define SECURITY_LEVEL_1                 0x01
#define SECURITY_LEVEL_2                 0x02

/* -- Buffers and transport ----------------------------- */
#define UDS_MAX_MSG_LEN                  512
#define UDS_MAX_DID_DATA                 64
#define UDS_MAX_DTC_COUNT                16
#define UDS_DTC_CODE_LEN                 3
#define UDS_MAX_SNAPSHOT_DIDS            8
#define UDS_SERVER_PORT                  13400   /* DoIP standard port */
#define UDS_RX_TIMEOUT_SEC               5

/* -- Timing parameters (ms) ---------------------------- */
#define P2_SERVER_DEFAULT                50
#define P2_STAR_SERVER_DEFAULT           5000
#define S3_SERVER_DEFAULT                5000

/* -- Command codes (uds_shell) ------------------------- */
#define CMD_SESSION      1
#define CMD_RESET        2
#define CMD_SECURITY     3
#define CMD_READ         4
#define CMD_WRITE        5
#define CMD_DTC          6
#define CMD_CLEAR        7
#define CMD_ROUTINE      8
#define CMD_DOWNLOAD     9
#define CMD_TP           10
#define CMD_HELP         11
#define CMD_QUIT         12

/* -- Helper macros ------------------------------------- */
#define ARRAY_SIZE(a)  (sizeof(a) / sizeof((a)[0]))

#endif /* UDS_COMMON_H */
