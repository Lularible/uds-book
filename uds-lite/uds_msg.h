/* uds_msg.h -- uds-lite message serialization layer
 * Responsibility: encoding/decoding UDS requests and responses, setting the
 * positive response bit 0x40, and the three-byte negative response format.
 */
#ifndef UDS_MSG_H
#define UDS_MSG_H
#include "uds_common.h"

/* -- Generic message type ------------------------------ */
typedef struct {
    u8 sid;                         /* SID or SID|0x40 (for positive response) */
    u8 sub_function;                /* Sub-function code (incl. suppressPosRsp bit) */
    u8  data[UDS_MAX_MSG_LEN];      /* Additional data */
    u16 data_len;                   /* Actual data length */
    u8  is_positive;                /* TRUE = positive response, FALSE = request */
} UdsMsg;

/* -- DID data record ----------------------------------- */
typedef struct {
    u16 did;
    u8  data[UDS_MAX_DID_DATA];
    u16 data_len;
    u8  writable;                  /* TRUE = writable via 0x2E */
    u8  session_required;          /* Minimum required session */
    u8  security_required;         /* Minimum required security level */
} DidRecord;

/* -- DTC record ---------------------------------------- */
typedef struct {
    u8  code[UDS_DTC_CODE_LEN];    /* 3-byte DTC code */
    u8  status;                    /* DTC status bits */
    u16 snapshot_dids[UDS_MAX_SNAPSHOT_DIDS];
    u8  snapshot_data[UDS_MAX_SNAPSHOT_DIDS][UDS_MAX_DID_DATA];
    u8  snapshot_count;
} DtcRecord;

/* -- Download transfer state --------------------------- */
typedef struct {
    u8  active;                    /* Whether a transfer is in progress */
    u8  direction;                 /* 0=download, 1=upload */
    u32 address;
    u32 remaining_size;
    u16 max_block_size;
    u8  expected_seq_counter;
} TransferState;

/* -- Routine definition -------------------------------- */
typedef struct {
    u16 rid;
    u8  activated;
    u8  current_status;
    s16 progress;
} RoutineState;

/* -- API ----------------------------------------------- */

/* Build a UDS request message. Called by client/shell. */
void uds_build_request(UdsMsg *msg, u8 sid, u8 sub_function,
                       const u8 *data, u16 data_len);

/* Parse a UDS message from raw bytes. Detects positive/negative response or request. */
void uds_parse_message(const u8 *raw, u16 raw_len, UdsMsg *msg);

/* Build a negative response byte sequence: {0x7F, original SID, NRC} */
u16 uds_build_negative_response(u8 *out, u8 original_sid, u8 nrc);

/* Build a positive response: SID|0x40 + variable data
 * (caller must place the sub-function byte in extra_data when required) */
u16 uds_build_positive_response(u8 *out, u8 request_sid,
                                const u8 *extra_data, u16 extra_len);

/* Extract a DID (2-byte big-endian) from a byte stream */
u16 uds_read_u16_be(const u8 *buf);

/* Write a u16 to a byte stream in big-endian order */
void uds_write_u16_be(u8 *buf, u16 val);

#endif /* UDS_MSG_H */
