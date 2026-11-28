/*
 * can_proto.c - CAN 应用层消息编解码实现
 *
 * ID 布局：node 占 bit10..4，cmd 占 bit3..0，因此节点上限 127、命令上限 15。
 * 解码不检查 DLC 与命令语义，只保证长度合法。
 */

#include "can_proto.h"

#include <stddef.h>

#include <string.h>

int can_proto_pack_id(uint8_t node, uint8_t cmd, uint32_t *id)
{
    if (id == NULL || node > 127U || cmd > 15U) {
        return -1;
    }
    *id = ((uint32_t)node << 4) | (uint32_t)cmd;
    return 0;
}

int can_proto_unpack_id(uint32_t id, uint8_t *node, uint8_t *cmd)
{
    if (id > 0x7FFU) {
        return -1;
    }
    if (node != NULL) {
        *node = (uint8_t)((id >> 4) & 0x7FU);
    }
    if (cmd != NULL) {
        *cmd = (uint8_t)(id & 0x0FU);
    }
    return 0;
}

int can_proto_encode(const can_proto_msg_t *msg, uint32_t *id,
                     uint8_t data[CAN_PROTO_MAX_DATA], uint8_t *len)
{
    if (msg == NULL || id == NULL || data == NULL || len == NULL) {
        return -1;
    }
    if (msg->len > CAN_PROTO_MAX_DATA) {
        return -1;
    }
    if (can_proto_pack_id(msg->node, msg->cmd, id) != 0) {
        return -1;
    }
    if (msg->len > 0U) {
        memcpy(data, msg->data, msg->len);
    }
    *len = msg->len;
    return 0;
}

int can_proto_decode(uint32_t id, const uint8_t *data, uint8_t len,
                     can_proto_msg_t *msg)
{
    if (data == NULL || msg == NULL || len > CAN_PROTO_MAX_DATA) {
        return -1;
    }
    if (can_proto_unpack_id(id, &msg->node, &msg->cmd) != 0) {
        return -1;
    }
    msg->len = len;
    if (len > 0U) {
        memcpy(msg->data, data, len);
    }
    return 0;
}

int can_proto_heartbeat_pack(uint8_t node, uint8_t state, uint16_t uptime_s,
                             uint32_t *id, uint8_t data[CAN_PROTO_MAX_DATA],
                             uint8_t *len)
{
    if (id == NULL || data == NULL || len == NULL) {
        return -1;
    }
    if (can_proto_pack_id(node, CAN_PROTO_CMD_HEARTBEAT, id) != 0) {
        return -1;
    }
    data[0] = state;
    data[1] = (uint8_t)(uptime_s & 0xFFU);
    data[2] = (uint8_t)((uptime_s >> 8) & 0xFFU);
    *len = 3U;
    return 0;
}
