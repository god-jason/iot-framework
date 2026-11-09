/*
 * can_proto_test.c - CAN 应用层协议测试（对应 can_proto.c）
 *
 * 覆盖：11 位 ID 打包/解析、消息编解码、心跳帧、越界与 NULL。
 */

#include "test.h"
#include "can_proto.h"

void ut_can_proto(void)
{
    can_proto_msg_t msg;
    can_proto_msg_t out;
    uint32_t id = 0;
    uint8_t node = 0;
    uint8_t cmd = 0;
    uint8_t data[CAN_PROTO_MAX_DATA];
    uint8_t len = 0;

    tf_begin("can_proto/ID 打包解析");

    TF_CHECK_INT(can_proto_pack_id(5U, 3U, &id), 0);
    TF_CHECK_INT(id, (5U << 4) | 3U);
    TF_CHECK_INT(can_proto_unpack_id(id, &node, &cmd), 0);
    TF_CHECK_INT(node, 5);
    TF_CHECK_INT(cmd, 3);

    /* 端点值 */
    TF_CHECK_INT(can_proto_pack_id(127U, 15U, &id), 0);
    TF_CHECK_INT(id, 0x7FFU);
    TF_CHECK_INT(can_proto_unpack_id(0x7FFU, &node, &cmd), 0);
    TF_CHECK_INT(node, 127);
    TF_CHECK_INT(cmd, 15);

    TF_CHECK_INT(can_proto_pack_id(128U, 0U, &id), -1);
    TF_CHECK_INT(can_proto_pack_id(0U, 16U, &id), -1);
    TF_CHECK_INT(can_proto_pack_id(0U, 0U, NULL), -1);
    TF_CHECK_INT(can_proto_unpack_id(0x800U, &node, &cmd), -1);

    /* 输出参数可空 */
    TF_CHECK_INT(can_proto_unpack_id(id, NULL, NULL), 0);

    tf_end();

    tf_begin("can_proto/消息编解码");

    memset(&msg, 0, sizeof(msg));
    msg.node = 7U;
    msg.cmd = CAN_PROTO_CMD_COMMAND;
    msg.len = 3U;
    msg.data[0] = 0x11U;
    msg.data[1] = 0x22U;
    msg.data[2] = 0x33U;

    TF_CHECK_INT(can_proto_encode(&msg, &id, data, &len), 0);
    TF_CHECK_INT(len, 3);
    TF_CHECK_INT(data[2], 0x33);

    TF_CHECK_INT(can_proto_decode(id, data, len, &out), 0);
    TF_CHECK_INT(out.node, 7);
    TF_CHECK_INT(out.cmd, CAN_PROTO_CMD_COMMAND);
    TF_CHECK_INT(out.len, 3);
    TF_CHECK_INT(out.data[0], 0x11);

    /* 零长度消息 */
    msg.len = 0U;
    TF_CHECK_INT(can_proto_encode(&msg, &id, data, &len), 0);
    TF_CHECK_INT(len, 0);

    msg.len = CAN_PROTO_MAX_DATA + 1U;
    TF_CHECK_INT(can_proto_encode(&msg, &id, data, &len), -1);

    TF_CHECK_INT(can_proto_encode(NULL, &id, data, &len), -1);
    TF_CHECK_INT(can_proto_encode(&msg, NULL, data, &len), -1);
    TF_CHECK_INT(can_proto_decode(id, NULL, 1U, &out), -1);
    TF_CHECK_INT(can_proto_decode(0x800U, data, 1U, &out), -1);
    TF_CHECK_INT(can_proto_decode(id, data, CAN_PROTO_MAX_DATA + 1U, &out), -1);

    tf_end();

    tf_begin("can_proto/心跳帧");

    TF_CHECK_INT(can_proto_heartbeat_pack(9U, 2U, 0x1234U, &id, data, &len), 0);
    TF_CHECK_INT(len, 3);
    TF_CHECK_INT(can_proto_unpack_id(id, &node, &cmd), 0);
    TF_CHECK_INT(node, 9);
    TF_CHECK_INT(cmd, CAN_PROTO_CMD_HEARTBEAT);
    TF_CHECK_INT(data[0], 2);
    TF_CHECK_INT(data[1], 0x34);
    TF_CHECK_INT(data[2], 0x12);

    TF_CHECK_INT(can_proto_heartbeat_pack(200U, 0U, 0U, &id, data, &len), -1);
    TF_CHECK_INT(can_proto_heartbeat_pack(1U, 0U, 0U, NULL, data, &len), -1);

    tf_end();
}
