#ifndef CH9120_REGISTER_H
#define CH9120_REGISTER_H

/*
 * CH9120 串口转以太网芯片配置协议定义（仅 UART 通道）。
 *
 * 帧格式：
 *   0x55 0xAA | CMD | LEN | DATA[LEN] | SUM
 *   SUM = CMD + LEN + DATA 的 8 位累加和
 *
 * 注意：CH9120 与 CH9121 配置命令集接近但不完全兼容，
 * 以下命令码 / 字段长度 / 字节序需按 datasheet/固件补充确认。
 */

#define CH9120_FRAME_HEAD0              0x55U
#define CH9120_FRAME_HEAD1              0xAAU
#define CH9120_FRAME_OVERHEAD           5U
#define CH9120_CMD_DATA_MAX             12U
#define CH9120_FRAME_MAX                (CH9120_FRAME_OVERHEAD + CH9120_CMD_DATA_MAX)

/* 命令（需按 datasheet/固件确认） */
#define CH9120_CMD_SET_IP               0x11U
#define CH9120_CMD_SET_SUBNET           0x12U
#define CH9120_CMD_SET_GATEWAY          0x13U
#define CH9120_CMD_SET_LOCAL_PORT       0x21U
#define CH9120_CMD_SET_DEST_IP          0x22U
#define CH9120_CMD_SET_DEST_PORT        0x23U
#define CH9120_CMD_SET_MODE             0x31U
#define CH9120_CMD_SET_BAUD             0x32U
#define CH9120_CMD_SAVE_RESET           0x0DU
#define CH9120_CMD_READ_CFG             0x41U

/* 工作模式 */
#define CH9120_MODE_TCP_SERVER          0x00U
#define CH9120_MODE_TCP_CLIENT          0x01U
#define CH9120_MODE_UDP_SERVER          0x02U
#define CH9120_MODE_UDP_CLIENT          0x03U

/* 超时 */
#define CH9120_CMD_TIMEOUT_MS           200U
#define CH9120_RESPONSE_TIMEOUT_MS      500U
#define CH9120_SAVE_RESET_MS            1000U

#endif /* CH9120_REGISTER_H */
