/* uds_msg.c — uds-lite 报文序列化实现
 */
#include <string.h>
#include "uds_msg.h"

void uds_build_request(UdsMsg *msg, u8 sid, u8 sub_function,
                       const u8 *data, u16 data_len)
{
    msg->sid = sid;
    msg->sub_function = sub_function;
    msg->is_positive = FALSE;
    msg->data_len = data_len;
    if (data && data_len > 0) {
        memcpy(msg->data, data, data_len);
    }
}

void uds_parse_message(const u8 *raw, u16 raw_len, UdsMsg *msg)
{
    if (raw_len < 1) return;

    u8 first_byte = raw[0];

    if (first_byte == NEGATIVE_RESPONSE_SID) {
        /* 负响应: {0x7F, 原SID, NRC} */
        msg->sid = (raw_len >= 2) ? raw[1] : 0;
        msg->sub_function = (raw_len >= 3) ? raw[2] : 0;
        msg->is_positive = FALSE;  /* 语义上不是正响应 */
        msg->data_len = 0;
    } else if (first_byte & SID_POSITIVE_RESPONSE_MASK) {
        /* 正响应: SID|0x40 开头 */
        msg->sid = first_byte & ~SID_POSITIVE_RESPONSE_MASK;
        msg->is_positive = TRUE;
        /* 后续字节为响应数据 */
        u16 remaining = raw_len - 1;
        if (remaining > UDS_MAX_MSG_LEN) remaining = UDS_MAX_MSG_LEN;
        if (remaining > 0) {
            memcpy(msg->data, &raw[1], remaining);
            msg->data_len = remaining;
        } else {
            msg->data_len = 0;
        }
    } else {
        /* 普通请求 (服务端收到诊断仪发来的请求) */
        msg->sid = first_byte;
        msg->is_positive = FALSE;
        msg->sub_function = (raw_len >= 2) ? (raw[1] & 0x7F) : 0;
        u16 remaining = (raw_len >= 2) ? raw_len - 2 : 0;
        if (remaining > UDS_MAX_MSG_LEN) remaining = UDS_MAX_MSG_LEN;
        if (remaining > 0) {
            memcpy(msg->data, &raw[2], remaining);
            msg->data_len = remaining;
        } else {
            msg->data_len = 0;
        }
    }
}

u16 uds_build_negative_response(u8 *out, u8 original_sid, u8 nrc)
{
    out[0] = NEGATIVE_RESPONSE_SID;
    out[1] = original_sid;
    out[2] = nrc;
    return 3;
}

u16 uds_build_positive_response(u8 *out, u8 request_sid,
                                const u8 *extra_data, u16 extra_len)
{
    u16 pos = 0;
    out[pos++] = request_sid | SID_POSITIVE_RESPONSE_MASK;
    if (extra_data && extra_len > 0) {
        memcpy(&out[pos], extra_data, extra_len);
        pos += extra_len;
    }
    return pos;
}

u16 uds_read_u16_be(const u8 *buf)
{
    return ((u16)buf[0] << 8) | buf[1];
}

void uds_write_u16_be(u8 *buf, u16 val)
{
    buf[0] = (val >> 8) & 0xFF;
    buf[1] = val & 0xFF;
}
