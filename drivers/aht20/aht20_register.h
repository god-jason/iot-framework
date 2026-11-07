#ifndef AHT20_REGISTER_H
#define AHT20_REGISTER_H

/* AHT20 / DHT20 寄存器与命令定义 */

#define AHT20_DEFAULT_ADDRESS       0x38U
#define AHT20_ADDRESS_ALT           0x39U

/* 命令 */
#define AHT20_CMD_STATUS            0x71U   /* 读状态字节 */
#define AHT20_CMD_SOFT_RESET        0xBAU   /* 软复位 */
#define AHT20_CMD_CALIBRATE         0xBEU   /* 校准 */
#define AHT20_CMD_TRIGGER           0xACU   /* 触发测量，后跟 0x33 0x00 */

/* 状态位 */
#define AHT20_STATUS_BUSY           0x80U   /* 1: 忙 */
#define AHT20_STATUS_CALIBRATED     0x08U   /* 1: 已校准 */

/* 时序（ms） */
#define AHT20_POWER_ON_DELAY_MS     40U
#define AHT20_RESET_DELAY_MS        20U
#define AHT20_CALIBRATE_DELAY_MS    10U
#define AHT20_MEASURE_DELAY_MS      80U

/* 原始数据分辨率 */
#define AHT20_DATA_SCALE            1048576.0f  /* 2^20 */

#endif /* AHT20_REGISTER_H */
