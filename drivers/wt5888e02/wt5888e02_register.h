#ifndef WT5888E02_REGISTER_H
#define WT5888E02_REGISTER_H

/*
 * WT5888E02 语音模块协议定义（UART 与一线串口两种控制方式）。
 *
 * UART 帧格式（常见 WT588F 系列固件）：
 *   0x7E | LEN | CMD | DATA[LEN] | SUM | 0xEF
 *   LEN 为 CMD + DATA 的字节数，SUM = LEN + CMD + DATA 的 8 位累加和
 *
 * 一线串口（单线）时序：
 *   拉低 5ms 启动一次传输，随后每 bit 以低电平脉宽区分：
 *   逻辑 0 = 低 200us，逻辑 1 = 低 600us，每 bit 后拉高 200us。
 *   每次传输 1 字节命令 + 2 字节数据，高位在前。
 *
 * 注意：命令码、帧长得随模组出厂固件不同而变化，
 * 以下常量为常见默认值，需按 datasheet/固件补充确认；
 * 未知命令可通过 wt5888e02_write 直接发送原始帧。
 */

#define WT5888E02_FRAME_HEAD            0x7EU
#define WT5888E02_FRAME_TAIL            0xEFU
#define WT5888E02_FRAME_OVERHEAD        5U    /* head + len + sum + tail + cmd */
#define WT5888E02_CMD_DATA_MAX          8U
#define WT5888E02_FRAME_MAX             (WT5888E02_FRAME_OVERHEAD + WT5888E02_CMD_DATA_MAX)

/* 命令（需按 datasheet/固件确认） */
#define WT5888E02_CMD_PLAY              0x03U  /* data: 2 字节曲目号（高字节在前） */
#define WT5888E02_CMD_STOP              0x04U
#define WT5888E02_CMD_PAUSE             0x05U
#define WT5888E02_CMD_RESUME            0x06U
#define WT5888E02_CMD_SET_VOLUME        0x0DU  /* data: 1 字节，0~30 */
#define WT5888E02_CMD_SET_LOOP          0x11U  /* data: 1 字节，0 单次 / 1 循环 */

/* 一线串口时序（us） */
#define WT5888E02_ONELINE_LATCH_US      5000U
#define WT5888E02_ONELINE_BIT0_LOW_US   200U
#define WT5888E02_ONELINE_BIT1_LOW_US   600U
#define WT5888E02_ONELINE_BIT_GAP_US    200U

/* 音量与忙检测 */
#define WT5888E02_VOLUME_MIN            0U
#define WT5888E02_VOLUME_MAX            30U
#define WT5888E02_BUSY_ACTIVE_LOW       1     /* BUSY 脚低有效（多数模组） */
#define WT5888E02_CMD_TIMEOUT_MS        100U
#define WT5888E02_START_DELAY_MS        20U

#endif /* WT5888E02_REGISTER_H */
