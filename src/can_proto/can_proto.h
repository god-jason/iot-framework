#ifndef CAN_PROTO_H
#define CAN_PROTO_H

/*
 * can_proto.h - CAN 应用层消息编解码
 *
 * 使用 11 位标准帧 ID：
 *   bit[10:4] = 节点号（0..127）
 *   bit[3:0]  = 命令字（0..15）
 * 数据段最多 8 字节，由上层定义各命令的负载布局。
 *
 * 本模块只做打包/解包，不访问 HAL CAN 驱动，便于复用与测试。
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CAN_PROTO_MAX_DATA       8U
#define CAN_PROTO_CMD_HEARTBEAT  0x0U
#define CAN_PROTO_CMD_COMMAND    0x1U
#define CAN_PROTO_CMD_RESPONSE   0x2U

typedef struct {
    uint8_t node;                        /* 节点号 */
    uint8_t cmd;                         /* 命令字 */
    uint8_t len;                         /* 数据长度 0..8 */
    uint8_t data[CAN_PROTO_MAX_DATA];    /* 负载 */
} can_proto_msg_t;

/* 打包 11 位 ID；节点或命令越界返回 -1 */
int can_proto_pack_id(uint8_t node, uint8_t cmd, uint32_t *id);

/* 解析 11 位 ID；ID 越界返回 -1 */
int can_proto_unpack_id(uint32_t id, uint8_t *node, uint8_t *cmd);

/*
 * 编码一条消息为 ID + 数据段。
 * 返回：0 成功；-1 参数非法或 len 超过 8。
 */
int can_proto_encode(const can_proto_msg_t *msg, uint32_t *id,
                     uint8_t data[CAN_PROTO_MAX_DATA], uint8_t *len);

/* 从 ID 与数据段解码一条消息 */
int can_proto_decode(uint32_t id, const uint8_t *data, uint8_t len,
                     can_proto_msg_t *msg);

/*
 * 心跳帧：cmd = HEARTBEAT，data[0] = 状态，data[1..2] = 运行秒数（小端）。
 * 返回：0 成功；-1 参数非法。
 */
int can_proto_heartbeat_pack(uint8_t node, uint8_t state, uint16_t uptime_s,
                             uint32_t *id, uint8_t data[CAN_PROTO_MAX_DATA],
                             uint8_t *len);

#ifdef __cplusplus
}
#endif

#endif /* CAN_PROTO_H */
