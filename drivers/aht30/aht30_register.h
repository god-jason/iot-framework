#ifndef AHT30_REGISTER_H
#define AHT30_REGISTER_H

/*
 * AHT30 命令与状态定义（I2C 0x38，命令与 AHT20 兼容）。
 *
 * 读取流程：
 *   0xAC 0x33 0x00 触发测量 -> 等待 80ms -> 读 7 字节
 *   [0]=状态 [1..3]=湿度 20 位 [3..5] 交叉=温度 20 位 [6]=CRC（AHT30 兼容）
 */

#define AHT30_DEFAULT_ADDRESS       0x38U
#define AHT30_ADDRESS_ALT           0x39U

/* 命令 */
#define AHT30_CMD_STATUS            0x71U   /* 读状态字节 */
#define AHT30_CMD_SOFT_RESET        0xBAU   /* 软复位 */
#define AHT30_CMD_CALIBRATE         0xBEU   /* 校准 */
#define AHT30_CMD_TRIGGER           0xACU   /* 触发测量，后跟 0x33 0x00 */

/* 状态位 */
#define AHT30_STATUS_BUSY           0x80U   /* 1: 忙 */
#define AHT30_STATUS_CALIBRATED     0x08U   /* 1: 已校准 */

/* 时序（ms） */
#define AHT30_POWER_ON_DELAY_MS     40U
#define AHT30_RESET_DELAY_MS        20U
#define AHT30_CALIBRATE_DELAY_MS    10U
#define AHT30_MEASURE_DELAY_MS      80U
#define AHT30_READ_DELAY_MS         10U

/* 原始数据为 20 位无符号（非补码），换算后减去偏移即得 ℃ */
#define AHT30_DATA_SCALE            1048576.0f   /* 2^20 */
#define AHT30_TEMP_SCALE            200.0f
#define AHT30_TEMP_OFFSET           (-50.0f)
#define AHT30_HUMIDITY_SCALE        100.0f

#endif /* AHT30_REGISTER_H */
