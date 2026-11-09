#ifndef ROBOT_PROTO_H
#define ROBOT_PROTO_H

/*
 * robot_proto.h - 上位机/外设二进制帧协议
 *
 * 帧格式（小端）：
 *   | AA 55 | seq | cmd | len_lo len_hi | payload[len] | crc_lo crc_hi |
 * CRC16-CCITT-FALSE 覆盖 seq..payload，初值 0xFFFF，多项式 0x1021。
 *
 * 提供编码、逐字节流式解析与命令分发表；无动态内存。
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ROBOT_PROTO_SYNC0        0xAAU
#define ROBOT_PROTO_SYNC1        0x55U
#define ROBOT_PROTO_MAX_PAYLOAD  64U
#define ROBOT_PROTO_HEADER_LEN   6U
#define ROBOT_PROTO_CRC_LEN      2U

typedef struct {
    uint8_t seq;                              /* 序号（回应时原样带回） */
    uint8_t cmd;                              /* 命令字 */
    uint16_t len;                             /* payload 长度 */
    uint8_t payload[ROBOT_PROTO_MAX_PAYLOAD]; /* 负载 */
} robot_frame_t;

/* CRC16-CCITT-FALSE */
uint16_t robot_crc16(const uint8_t *data, uint32_t len);

/*
 * 编码一帧。
 * 返回：帧总长度；payload 超长、输出空间不足或参数非法返回 -1。
 */
int robot_frame_encode(const robot_frame_t *frame, uint8_t *out, uint32_t cap);

/* 流式解析器 */
typedef struct {
    uint8_t state;                 /* 内部状态机 */
    uint16_t index;                /* payload 写入位置 */
    uint16_t len;                  /* 本帧 payload 长度 */
    uint8_t crc_lo;                /* 待比较的 CRC 低字节 */
    robot_frame_t frame;           /* 正在接收的帧 */
} robot_parser_t;

void robot_parser_init(robot_parser_t *parser);

/*
 * 输入一个字节。
 * 返回：1 表示解析到完整且 CRC 正确的帧（写入 out）；
 *       0 表示仍需继续输入；-1 表示该帧校验失败并已复位。
 * out 可为 NULL（只做校验）。
 */
int robot_parser_feed(robot_parser_t *parser, uint8_t byte, robot_frame_t *out);

/* 命令处理回调：返回 0 表示已处理 */
typedef int (*robot_cmd_fn)(const robot_frame_t *request, robot_frame_t *response,
                            void *user);

#define ROBOT_CMD_TABLE_MAX 24U

typedef struct {
    uint8_t cmd;
    robot_cmd_fn fn;
    void *user;
    bool used;
} robot_cmd_entry_t;

typedef struct {
    robot_cmd_entry_t entries[ROBOT_CMD_TABLE_MAX];
} robot_cmd_table_t;

void robot_cmd_table_init(robot_cmd_table_t *table);

/* 注册命令；重复命令字或表满返回 -1 */
int robot_cmd_register(robot_cmd_table_t *table, uint8_t cmd,
                       robot_cmd_fn fn, void *user);

/*
 * 分发一帧命令：找到处理函数后调用，response 由处理函数填写。
 * 返回：处理函数返回值；未注册命令返回 -1。
 */
int robot_cmd_dispatch(robot_cmd_table_t *table, const robot_frame_t *request,
                       robot_frame_t *response);

#ifdef __cplusplus
}
#endif

#endif /* ROBOT_PROTO_H */
