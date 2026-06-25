/* uds_msg.h — uds-lite 报文序列化层
 * 职责: UDS 请求/响应的编解码，正响应位 0x40 置位，负响应三字节格式。
 */
#ifndef UDS_MSG_H
#define UDS_MSG_H
#include "uds_common.h"

/* ── 通用消息类型 ────────────────────────── */
typedef struct {
    u8 sid;                         /* SID 或 SID|0x40 (正响应时) */
    u8 sub_function;                /* 子功能码 (含 suppressPosRsp 位) */
    u8  data[UDS_MAX_MSG_LEN];     /* 附加数据 */
    u16 data_len;                  /* 数据实际长度 */
    u8  is_positive;               /* TRUE = 正响应, FALSE = 请求 */
} UdsMsg;

/* ── DID 数据记录 ────────────────────────── */
typedef struct {
    u16 did;
    u8  data[UDS_MAX_DID_DATA];
    u16 data_len;
    u8  writable;                  /* TRUE = 支持 0x2E 写入 */
    u8  session_required;          /* 最低会话要求 */
    u8  security_required;         /* 最低安全等级 */
} DidRecord;

/* ── DTC 记录 ────────────────────────────── */
typedef struct {
    u8  code[UDS_DTC_CODE_LEN];    /* 3字节 DTC 编码 */
    u8  status;                    /* DTC 状态位 */
    u16 snapshot_dids[UDS_MAX_SNAPSHOT_DIDS];
    u8  snapshot_data[UDS_MAX_SNAPSHOT_DIDS][UDS_MAX_DID_DATA];
    u8  snapshot_count;
} DtcRecord;

/* ── 下载传输状态 ────────────────────────── */
typedef struct {
    u8  active;                    /* 传输是否正在进行 */
    u8  direction;                 /* 0=下载, 1=上传 */
    u32 address;
    u32 remaining_size;
    u16 max_block_size;
    u8  expected_seq_counter;
} TransferState;

/* ── 例行程序定义 ────────────────────────── */
typedef struct {
    u16 rid;
    u8  activated;
    u8  current_status;
    s16 progress;
} RoutineState;

/* ── API ─────────────────────────────────── */

/* 构建 UDS 请求消息。此函数被 client/shell 调用。 */
void uds_build_request(UdsMsg *msg, u8 sid, u8 sub_function,
                       const u8 *data, u16 data_len);

/* 从原始字节解析 UDS 消息。判断正响应/负响应/请求类型。 */
void uds_parse_message(const u8 *raw, u16 raw_len, UdsMsg *msg);

/* 构建负响应字节序列: {0x7F, 原SID, NRC} */
u16 uds_build_negative_response(u8 *out, u8 original_sid, u8 nrc);

/* 构建正响应: SID|0x40 + 可变数据 (包含子功能字节时需调用方放入extra_data) */
u16 uds_build_positive_response(u8 *out, u8 request_sid,
                                const u8 *extra_data, u16 extra_len);

/* 从字节流中提取 DID (2字节大端序) */
u16 uds_read_u16_be(const u8 *buf);

/* 将 u16 以大端序写入字节流 */
void uds_write_u16_be(u8 *buf, u16 val);

#endif /* UDS_MSG_H */
