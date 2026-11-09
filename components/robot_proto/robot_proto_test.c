/*
 * robot_proto_test.c - 上位机帧协议测试（对应 robot_proto.c）
 *
 * 覆盖：CRC16-CCITT、帧编码、逐字节流式解析、坏 CRC 与超长帧、
 *       命令注册与分发。
 */

#include "test.h"
#include "robot_proto.h"

static int s_cmd_count;

static int echo_handler(const robot_frame_t *request, robot_frame_t *response,
                        void *user)
{
    (void)user;
    s_cmd_count++;
    if (response != NULL) {
        response->seq = request->seq;
        response->cmd = (uint8_t)(request->cmd | 0x80U);
        response->len = request->len;
        memcpy(response->payload, request->payload, request->len);
    }
    return 0;
}

void ut_robot_proto(void)
{
    robot_frame_t frame;
    robot_frame_t parsed;
    robot_parser_t parser;
    robot_cmd_table_t table;
    uint8_t buf[160];
    int len;
    int i;
    int got;

    tf_begin("robot_proto/CRC16-CCITT");

    TF_CHECK_INT(robot_crc16((const uint8_t *)"123456789", 9U), 0x29B1);
    TF_CHECK_INT(robot_crc16(NULL, 9U), 0xFFFF);
    TF_CHECK_INT(robot_crc16((const uint8_t *)"1", 0U), 0xFFFF);

    tf_end();

    tf_begin("robot_proto/帧编码");

    frame.seq = 7U;
    frame.cmd = 0x10U;
    frame.len = 3U;
    frame.payload[0] = 1U;
    frame.payload[1] = 2U;
    frame.payload[2] = 3U;

    len = robot_frame_encode(&frame, buf, sizeof(buf));
    TF_CHECK_INT(len, (int)(ROBOT_PROTO_HEADER_LEN + 3U + ROBOT_PROTO_CRC_LEN));
    TF_CHECK_INT(buf[0], ROBOT_PROTO_SYNC0);
    TF_CHECK_INT(buf[1], ROBOT_PROTO_SYNC1);
    TF_CHECK_INT(buf[2], 7);
    TF_CHECK_INT(buf[3], 0x10);
    TF_CHECK_INT(buf[4], 3);
    TF_CHECK_INT(buf[5], 0);

    TF_CHECK_INT(robot_frame_encode(&frame, buf, 4U), -1);   /* 空间不足 */
    TF_CHECK_INT(robot_frame_encode(NULL, buf, sizeof(buf)), -1);
    TF_CHECK_INT(robot_frame_encode(&frame, NULL, sizeof(buf)), -1);

    frame.len = ROBOT_PROTO_MAX_PAYLOAD + 1U;
    TF_CHECK_INT(robot_frame_encode(&frame, buf, sizeof(buf)), -1);

    tf_end();

    tf_begin("robot_proto/流式解析");

    frame.len = 3U;
    len = robot_frame_encode(&frame, buf, sizeof(buf));
    robot_parser_init(&parser);

    got = 0;
    for (i = 0; i < len; i++) {
        int rc = robot_parser_feed(&parser, buf[i], &parsed);
        if (i < len - 1) {
            TF_CHECK_INT(rc, 0);
        } else {
            got = rc;
        }
    }
    TF_CHECK_INT(got, 1);
    TF_CHECK_INT(parsed.seq, 7);
    TF_CHECK_INT(parsed.cmd, 0x10);
    TF_CHECK_INT(parsed.len, 3);
    TF_CHECK_INT(parsed.payload[2], 3);

    /* 噪声字节后仍能重新同步 */
    robot_parser_init(&parser);
    TF_CHECK_INT(robot_parser_feed(&parser, 0x00, NULL), 0);
    TF_CHECK_INT(robot_parser_feed(&parser, 0xFF, NULL), 0);
    got = 0;
    for (i = 0; i < len; i++) {
        int rc = robot_parser_feed(&parser, buf[i], &parsed);
        if (rc != 0) {
            got = rc;
        }
    }
    TF_CHECK_INT(got, 1);

    tf_end();

    tf_begin("robot_proto/坏 CRC 与超长");

    frame.len = 3U;
    len = robot_frame_encode(&frame, buf, sizeof(buf));
    buf[6] ^= 0xFFU;                                   /* 破坏负载 */
    robot_parser_init(&parser);
    got = 0;
    for (i = 0; i < len; i++) {
        int rc = robot_parser_feed(&parser, buf[i], NULL);
        if (rc != 0) {
            got = rc;
        }
    }
    TF_CHECK_INT(got, -1);

    /* 声明长度超过上限 */
    robot_parser_init(&parser);
    TF_CHECK_INT(robot_parser_feed(&parser, ROBOT_PROTO_SYNC0, NULL), 0);
    TF_CHECK_INT(robot_parser_feed(&parser, ROBOT_PROTO_SYNC1, NULL), 0);
    TF_CHECK_INT(robot_parser_feed(&parser, 1U, NULL), 0);
    TF_CHECK_INT(robot_parser_feed(&parser, 2U, NULL), 0);
    TF_CHECK_INT(robot_parser_feed(&parser, 0U, NULL), 0);
    TF_CHECK_INT(robot_parser_feed(&parser,
                                   (uint8_t)(ROBOT_PROTO_MAX_PAYLOAD + 1U),
                                   NULL), -1);

    TF_CHECK_INT(robot_parser_feed(NULL, 0U, NULL), -1);

    tf_end();

    tf_begin("robot_proto/命令分发");

    robot_cmd_table_init(&table);
    s_cmd_count = 0;

    TF_CHECK_INT(robot_cmd_register(&table, 0x10U, echo_handler, NULL), 0);
    TF_CHECK_INT(robot_cmd_register(&table, 0x10U, echo_handler, NULL), -1);
    TF_CHECK_INT(robot_cmd_register(&table, 0x11U, NULL, NULL), -1);
    TF_CHECK_INT(robot_cmd_register(NULL, 0x11U, echo_handler, NULL), -1);

    frame.cmd = 0x10U;
    frame.len = 2U;
    frame.payload[0] = 0xAAU;
    frame.payload[1] = 0x55U;
    memset(&parsed, 0, sizeof(parsed));

    TF_CHECK_INT(robot_cmd_dispatch(&table, &frame, &parsed), 0);
    TF_CHECK_INT(s_cmd_count, 1);
    TF_CHECK_INT(parsed.cmd, 0x90);
    TF_CHECK_INT(parsed.payload[1], 0x55);

    frame.cmd = 0x20U;
    TF_CHECK_INT(robot_cmd_dispatch(&table, &frame, &parsed), -1);
    TF_CHECK_INT(robot_cmd_dispatch(NULL, &frame, &parsed), -1);
    TF_CHECK_INT(robot_cmd_dispatch(&table, NULL, &parsed), -1);

    tf_end();
}
