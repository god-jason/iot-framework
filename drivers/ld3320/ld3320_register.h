#ifndef LD3320_REGISTER_H
#define LD3320_REGISTER_H

/*
 * LD3320 语音识别芯片（SPI / 并口）寄存器访问协议定义。
 *
 * SPI 每次访问为 3 字节报文：CMD | ADDR | DATA
 *   - 写：CMD = 0x35，随后 ADDR、DATA
 *   - 读：CMD = 0x25，随后 ADDR，再读取 1 字节（MISO）
 *
 * 注意：上述 SPI 命令字与寄存器地址需按 datasheet/固件补充确认；
 * 语音识别还要求向芯片写入 DSP 固件与多步寄存器序列，
 * 本驱动只提供基础读写与启动/停止/取结果的框架。
 */

#define LD3320_SPI_CMD_WRITE            0x35U
#define LD3320_SPI_CMD_READ             0x25U

/* 常用寄存器（需按 datasheet/固件确认） */
#define LD3320_REG_CHIP_ID              0x07U
#define LD3320_REG_CMD                  0x00U  /* 命令寄存器 */
#define LD3320_REG_STATUS               0x01U
#define LD3320_REG_ASR_RESULT           0x02U  /* 识别结果索引 */
#define LD3320_REG_ASR_FLAG             0x03U  /* 识别完成标志 */
#define LD3320_REG_PLAY_CTRL            0x04U  /* 播放控制 */
#define LD3320_REG_VOLUME               0x05U
#define LD3320_REG_IRQ_FLAG             0x06U

/* 命令值（需按 datasheet/固件确认） */
#define LD3320_CMD_ASR_START            0x01U
#define LD3320_CMD_ASR_STOP             0x02U
#define LD3320_CMD_PLAY_START           0x03U
#define LD3320_CMD_PLAY_STOP            0x04U
#define LD3320_CMD_RESET                0x00U

/* 状态位 */
#define LD3320_STATUS_READY             0x01U
#define LD3320_STATUS_BUSY              0x02U
#define LD3320_ASR_FLAG_DONE            0x01U

/* 时序（ms） */
#define LD3320_RESET_LOW_MS             10U
#define LD3320_RESET_WAIT_MS            50U
#define LD3320_START_WAIT_MS            20U
#define LD3320_RESULT_TIMEOUT_MS        5000U
#define LD3320_POLL_MS                  20U

/* 音量范围 */
#define LD3320_VOLUME_MIN               0U
#define LD3320_VOLUME_MAX               15U

#endif /* LD3320_REGISTER_H */
