#ifndef AHT40_REGISTER_H
#define AHT40_REGISTER_H

/*
 * AHT40 命令与状态定义（I2C 0x38，命令与 AHT20/AHT30 兼容）。
 *
 * 读取流程：
 *   0xAC 0x33 0x00 触发测量 -> 等待 80ms -> 读 7 字节
 *   [0]=状态 [1..3]=湿度 20 位 [3..5] 交叉=温度 20 位 [6]=保留/CRC
 *
 * 说明：AHT40 的数据帧与 AHT20 相同（无强制 CRC），
 *       状态字节 bit7 为忙、bit3 为“已校准”。
 */

#define AHT40_DEFAULT_ADDRESS       0x38U
#define AHT40_ADDRESS_ALT           0x39U

/* 命令 */
#define AHT40_CMD_STATUS            0x71U
#define AHT40_CMD_SOFT_RESET        0xBAU
#define AHT40_CMD_CALIBRATE         0xBEU
#define AHT40_CMD_TRIGGER           0xACU   /* 后跟 0x33 0x00 */

/* 状态位 */
#define AHT40_STATUS_BUSY           0x80U
#define AHT40_STATUS_CALIBRATED     0x08U

/* 时序（ms） */
#define AHT40_POWER_ON_DELAY_MS     40U
#define AHT40_RESET_DELAY_MS        20U
#define AHT40_CALIBRATE_DELAY_MS    10U
#define AHT40_MEASURE_DELAY_MS      80U
#define AHT40_READ_DELAY_MS         10U

/* 20 位无符号原始值换算 */
#define AHT40_DATA_SCALE            1048576.0f   /* 2^20 */
#define AHT40_TEMP_SCALE            200.0f
#define AHT40_TEMP_OFFSET           (-50.0f)
#define AHT40_HUMIDITY_SCALE        100.0f

#endif /* AHT40_REGISTER_H */
