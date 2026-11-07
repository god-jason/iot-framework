#ifndef DHT20_REGISTER_H
#define DHT20_REGISTER_H

/*
 * DHT20 命令与状态定义（I2C 0x38，协议与 AHT20 兼容）。
 *
 * 读取流程：
 *   0xAC 0x33 0x00 触发测量 -> 等待 80ms -> 读 7 字节
 *   [0]=状态 [1..5]=20 位湿度 + 20 位温度 [6]=CRC8（部分模组不提供）
 *
 * CRC8 参数：poly 0x31, init 0xFF，覆盖前 6 字节。
 * 由于并非所有 DHT20 模组都返回 CRC，驱动在初始化时试读一帧判断是否可用，
 * 可用才强制校验。
 */

#define DHT20_DEFAULT_ADDRESS       0x38U
#define DHT20_ADDRESS_ALT           0x39U

/* 命令 */
#define DHT20_CMD_STATUS            0x71U
#define DHT20_CMD_SOFT_RESET        0xBAU
#define DHT20_CMD_CALIBRATE         0xBEU
#define DHT20_CMD_TRIGGER           0xACU   /* 后跟 0x33 0x00 */

/* 状态位 */
#define DHT20_STATUS_BUSY           0x80U
#define DHT20_STATUS_CALIBRATED     0x08U

/* 时序（ms） */
#define DHT20_POWER_ON_DELAY_MS     40U
#define DHT20_RESET_DELAY_MS        20U
#define DHT20_CALIBRATE_DELAY_MS    10U
#define DHT20_MEASURE_DELAY_MS      80U
#define DHT20_READ_DELAY_MS         10U

/* 20 位无符号原始值换算 */
#define DHT20_DATA_SCALE            1048576.0f   /* 2^20 */
#define DHT20_TEMP_SCALE            200.0f
#define DHT20_TEMP_OFFSET           (-50.0f)
#define DHT20_HUMIDITY_SCALE        100.0f

/* CRC8：poly 0x31, init 0xFF */
#define DHT20_CRC_POLY              0x31U
#define DHT20_CRC_INIT              0xFFU

#endif /* DHT20_REGISTER_H */
