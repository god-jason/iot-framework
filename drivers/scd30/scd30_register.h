#ifndef SCD30_REGISTER_H
#define SCD30_REGISTER_H

/*
 * SCD30 NDIR CO2 / 温湿度传感器——命令式 I2C，默认地址 0x61。
 *
 * 命令为 16 位，命令本身不带 CRC；返回的数据按 16 位字给出，
 * 每个字后跟 1 字节 CRC8。测量结果为 3 个 IEEE754 大端 float。
 */

#define SCD30_DEFAULT_ADDRESS            0x61U

/* 命令 */
#define SCD30_CMD_TRIGGER_CONTINUOUS     0x0010U  /* 参数：测量间隔 s（2..1800） */
#define SCD30_CMD_STOP_CONTINUOUS        0x0104U
#define SCD30_CMD_GET_DATA_READY         0x0202U
#define SCD30_CMD_READ_MEASUREMENT       0x0300U  /* 18 字节：CO2 / T / RH */
#define SCD30_CMD_GET_FIRMWARE_VERSION   0x2100U  /* 1 个数据字，用于 ID 校验 */
#define SCD30_CMD_SET_TEMPERATURE_OFFSET 0x5403U  /* 参数：0.01℃ 单位 */
#define SCD30_CMD_SET_ALTITUDE           0x5102U  /* 参数：海拔 m */
#define SCD30_CMD_SET_FORCED_RECAL       0x5204U  /* 参数：参考 CO2 ppm */
#define SCD30_CMD_SET_ASC                0x5306U  /* 参数：1 使能，0 关闭 */
#define SCD30_CMD_SOFT_RESET             0xD304U

/* 时序（ms） */
#define SCD30_RESET_DELAY_MS         30U
#define SCD30_START_DELAY_MS         20U
#define SCD30_READ_TIMEOUT_MS        200U

/* CRC8：poly 0x31, init 0xFF, MSB first */
#define SCD30_CRC_POLY               0x31U
#define SCD30_CRC_INIT               0xFFU

/* 默认测量间隔与数据长度 */
#define SCD30_DEFAULT_INTERVAL_S     5U
#define SCD30_MEASUREMENT_BYTES      18U

#endif /* SCD30_REGISTER_H */
