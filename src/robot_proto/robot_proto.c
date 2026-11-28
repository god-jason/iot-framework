/*
 * robot_proto.c - 二进制帧协议实现
 *
 * 解析器状态机按字节推进：帧头 -> seq -> cmd -> len -> payload -> crc。
 * 任意阶段失败都会复位到等待帧头，保证后续数据可重新同步。
 */

#include "robot_proto.h"

#include <stddef.h>

#include <string.h>

/* 解析状态 */
enum {
    RP_SYNC0 = 0,
    RP_SYNC1,
    RP_SEQ,
    RP_CMD,
    RP_LEN0,
    RP_LEN1,
    RP_PAYLOAD,
    RP_CRC0,
    RP_CRC1
};

uint16_t robot_crc16(const uint8_t *data, uint32_t len)
{
    uint16_t crc = 0xFFFFU;
    uint32_t i;
    int bit;

    if (data == NULL) {
        return crc;
    }

    for (i = 0U; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (bit = 0; bit < 8; bit++) {
            if ((crc & 0x8000U) != 0U) {
                crc = (uint16_t)((crc << 1) ^ 0x1021U);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

int robot_frame_encode(const robot_frame_t *frame, uint8_t *out, uint32_t cap)
{
    uint32_t total;
    uint16_t crc;
    uint32_t off;

    if (frame == NULL || out == NULL ||
        frame->len > ROBOT_PROTO_MAX_PAYLOAD) {
        return -1;
    }

    total = (uint32_t)ROBOT_PROTO_HEADER_LEN + frame->len + ROBOT_PROTO_CRC_LEN;
    if (cap < total) {
        return -1;
    }

    out[0] = ROBOT_PROTO_SYNC0;
    out[1] = ROBOT_PROTO_SYNC1;
    out[2] = frame->seq;
    out[3] = frame->cmd;
    out[4] = (uint8_t)(frame->len & 0xFFU);
    out[5] = (uint8_t)((frame->len >> 8) & 0xFFU);

    if (frame->len > 0U) {
        memcpy(&out[ROBOT_PROTO_HEADER_LEN], frame->payload, frame->len);
    }

    crc = robot_crc16(&out[2], 4U + frame->len);
    off = (uint32_t)ROBOT_PROTO_HEADER_LEN + frame->len;
    out[off] = (uint8_t)(crc & 0xFFU);
    out[off + 1U] = (uint8_t)((crc >> 8) & 0xFFU);

    return (int)total;
}

void robot_parser_init(robot_parser_t *parser)
{
    if (parser == NULL) {
        return;
    }
    memset(parser, 0, sizeof(*parser));
    parser->state = RP_SYNC0;
}

int robot_parser_feed(robot_parser_t *parser, uint8_t byte, robot_frame_t *out)
{
    if (parser == NULL) {
        return -1;
    }

    switch (parser->state) {
    case RP_SYNC0:
        if (byte == ROBOT_PROTO_SYNC0) {
            parser->state = RP_SYNC1;
        }
        break;

    case RP_SYNC1:
        if (byte == ROBOT_PROTO_SYNC1) {
            parser->state = RP_SEQ;
        } else if (byte == ROBOT_PROTO_SYNC0) {
            /* 保持同步字节，等待下一个 0x55 */
        } else {
            parser->state = RP_SYNC0;
        }
        break;

    case RP_SEQ:
        parser->frame.seq = byte;
        parser->state = RP_CMD;
        break;

    case RP_CMD:
        parser->frame.cmd = byte;
        parser->state = RP_LEN0;
        break;

    case RP_LEN0:
        parser->len = byte;
        parser->state = RP_LEN1;
        break;

    case RP_LEN1:
        parser->len |= (uint16_t)byte << 8;
        if (parser->len > ROBOT_PROTO_MAX_PAYLOAD) {
            parser->state = RP_SYNC0;
            return -1;
        }
        parser->frame.len = parser->len;
        parser->index = 0U;
        parser->state = (parser->len == 0U) ? RP_CRC0 : RP_PAYLOAD;
        break;

    case RP_PAYLOAD:
        parser->frame.payload[parser->index++] = byte;
        if (parser->index >= parser->len) {
            parser->state = RP_CRC0;
        }
        break;

    case RP_CRC0:
        parser->crc_lo = byte;
        parser->state = RP_CRC1;
        break;

    case RP_CRC1: {
        uint16_t calc;
        uint16_t recv;
        uint8_t buf[4U + ROBOT_PROTO_MAX_PAYLOAD];

        buf[0] = parser->frame.seq;
        buf[1] = parser->frame.cmd;
        buf[2] = (uint8_t)(parser->frame.len & 0xFFU);
        buf[3] = (uint8_t)((parser->frame.len >> 8) & 0xFFU);
        if (parser->frame.len > 0U) {
            memcpy(&buf[4], parser->frame.payload, parser->frame.len);
        }

        calc = robot_crc16(buf, 4U + parser->frame.len);
        recv = (uint16_t)parser->crc_lo | ((uint16_t)byte << 8);

        parser->state = RP_SYNC0;

        if (calc != recv) {
            return -1;
        }
        if (out != NULL) {
            *out = parser->frame;
        }
        return 1;
    }

    default:
        parser->state = RP_SYNC0;
        break;
    }

    return 0;
}

void robot_cmd_table_init(robot_cmd_table_t *table)
{
    if (table == NULL) {
        return;
    }
    memset(table, 0, sizeof(*table));
}

int robot_cmd_register(robot_cmd_table_t *table, uint8_t cmd,
                       robot_cmd_fn fn, void *user)
{
    uint32_t i;

    if (table == NULL || fn == NULL) {
        return -1;
    }
    for (i = 0U; i < ROBOT_CMD_TABLE_MAX; i++) {
        if (table->entries[i].used && table->entries[i].cmd == cmd) {
            return -1;
        }
    }
    for (i = 0U; i < ROBOT_CMD_TABLE_MAX; i++) {
        if (!table->entries[i].used) {
            table->entries[i].cmd = cmd;
            table->entries[i].fn = fn;
            table->entries[i].user = user;
            table->entries[i].used = true;
            return 0;
        }
    }
    return -1;
}

int robot_cmd_dispatch(robot_cmd_table_t *table, const robot_frame_t *request,
                       robot_frame_t *response)
{
    uint32_t i;

    if (table == NULL || request == NULL) {
        return -1;
    }
    for (i = 0U; i < ROBOT_CMD_TABLE_MAX; i++) {
        if (table->entries[i].used &&
            table->entries[i].cmd == request->cmd) {
            return table->entries[i].fn(request, response,
                                        table->entries[i].user);
        }
    }
    return -1;
}
