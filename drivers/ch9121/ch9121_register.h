#ifndef CH9121_REGISTER_H
#define CH9121_REGISTER_H

/*
 * CH9121 串口转以太网芯片配置协议定义。
 *
 * 帧格式（UART 配置通道）：
 *   0x55 0xAA | CMD | LEN | DATA[LEN] | SUM
 *   SUM = CMD + LEN + DATA 的 8 位累加和
 *
 * 注意：以下命令码、参数长度与字节序需按 datasheet/固件补充确认，
 * 本驱动实现的是基础框架，字段顺序按常见固件版本给出。
 */

#define CH9121_FRAME_HEAD0              0x55U
#define CH9121_FRAME_HEAD1              0xAAU
#define CH9121_FRAME_OVERHEAD           5U    /* 2 头 + cmd + len + sum */
#define CH9121_CMD_DATA_MAX             16U
#define CH9121_FRAME_MAX                (CH9121_FRAME_OVERHEAD + CH9121_CMD_DATA_MAX)

/* 配置命令（需按 datasheet/固件确认） */
#define CH9121_CMD_SET_MAC              0x01U
#define CH9121_CMD_SET_IP               0x11U  /* data: 4 字节 IPv4 */
#define CH9121_CMD_SET_SUBNET           0x12U  /* data: 4 字节 */
#define CH9121_CMD_SET_GATEWAY          0x13U  /* data: 4 字节 */
#define CH9121_CMD_SET_LOCAL_PORT       0x21U  /* data: 2 字节（小端） */
#define CH9121_CMD_SET_DEST_IP          0x22U  /* data: 4 字节 */
#define CH9121_CMD_SET_DEST_PORT        0x23U  /* data: 2 字节（小端） */
#define CH9121_CMD_SET_MODE             0x31U  /* data: 1 字节工作模式 */
#define CH9121_CMD_SET_BAUD             0x32U  /* data: 4 字节波特率（大端） */
#define CH9121_CMD_SAVE_RESET           0x0DU  /* 保存参数并重启 */
#define CH9121_CMD_READ_CFG             0x41U  /* 读取参数，回复帧格式同写 */

/* 工作模式（需按 datasheet/固件确认） */
#define CH9121_MODE_TCP_SERVER          0x00U
#define CH9121_MODE_TCP_CLIENT          0x01U
#define CH9121_MODE_UDP_SERVER          0x02U
#define CH9121_MODE_UDP_CLIENT          0x03U

/* 响应与超时 */
#define CH9121_RESPONSE_TIMEOUT_MS      500U
#define CH9121_SAVE_RESET_MS            1000U
#define CH9121_CMD_TIMEOUT_MS           200U
#define CH9121_DEFAULT_BAUDRATE         115200UL

#endif /* CH9121_REGISTER_H */
