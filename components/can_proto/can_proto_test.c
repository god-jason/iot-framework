/* can_proto 单元测试：ID 打包、消息编解码与心跳帧。 */
#include "test.h"
#include "can_proto.h"

void ut_can_proto(void)
{
    can_proto_msg_t msg;
    can_proto_msg_t out;
    uint32_t id;
    uint8_t node;
    uint8_t cmd;
    uint8_t data[CAN_PROTO_MAX_DATA];
    uint8_t len;

    tf_begin("can_proto/ID 打包与解析");
    TF_CHECK_INT(can_proto_pack_id(5U, 3U, &id), 0);
    TF_CHECK_INT(id, (5U << 4) | 3U);
    TF_CHECK_INT(can_proto_unpack_id(id, &node, &cmd), 0);
    TF_CHECK_INT(node, 5);
    TF_CHECK_INT(cmd, 3);
    TF_CHECK_INT(can_proto_pack_id(128U, 0U, &id), -1);
    TF_CHECK_INT(can_proto_pack_id(0U, 16U, &id), -1);
    TF_CHECK_INT(can_proto_unpack_id(0x800U, &node, &cmd), -1);
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
    TF_CHECK_INT(can_proto_decode(id, data, len, &out), 0);
    TF_CHECK_INT(out.node, 7);
    TF_CHECK_INT(out.cmd, CAN_PROTO_CMD_COMMAND);
    TF_CHECK_INT(out.len, 3);
    TF_CHECK_INT(out.data[2], 0x33);

    msg.len = CAN_PROTO_MAX_DATA + 1U;
    TF_CHECK_INT(can_proto_encode(&msg, &id, data, &len), -1);
    TF_CHECK_INT(can_proto_decode(0x800U, data, 1U, &out), -1);
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
    tf_end();
}
