#ifndef SCD4X_REGISTER_H
#define SCD4X_REGISTER_H

/*
 * SCD40 / SCD41 NDIR CO2 传感器——命令式 I2C，默认地址 0x62。
 *
 * 与 SCD30 不同：SCD4x 的命令字本身也要跟随 1 字节 CRC8，
 * 每个 16 位数据字同样跟随 1 字节 CRC8。
 */

#define SCD4X_DEFAULT_ADDRESS            0x62U

/* 命令 */
#define SCD4X_CMD_START_PERIODIC         0x21B1U
#define SCD4X_CMD_STOP_PERIODIC          0x21ACU
#define SCD4X_CMD_GET_DATA_READY         0xE4B8U
#define SCD4X_CMD_READ_MEASUREMENT       0xEC05U  /* 9 字节：CO2 / T / RH */
#define SCD4X_CMD_GET_SERIAL             0xE102U  /* 3 个数据字 */
#define SCD4X_CMD_REINIT                 0x3650U
#define SCD4X_CMD_FACTORY_RESET          0x3646U
#define SCD4X_CMD_SELF_TEST              0x3639U
#define SCD4X_CMD_SET_TEMP_OFFSET        0x241DU  /* 参数：0.01℃ */
#define SCD4X_CMD_SET_ALTITUDE           0x2427U  /* 参数：海拔 m */
#define SCD4X_CMD_SET_AMBIENT_PRESSURE   0xE000U  /* 参数：Pa */
#define SCD4X_CMD_SET_ASC                0x2416U  /* 参数：1 使能 */
#define SCD4X_CMD_FORCED_RECAL           0x362FU  /* 参数：参考 ppm */
#define SCD4X_CMD_POWER_DOWN             0x36E0U
#define SCD4X_CMD_WAKE_UP                0x36F6U

/* 时序（ms） */
#define SCD4X_WAKE_DELAY_MS          30U
#define SCD4X_STOP_DELAY_MS          500U
#define SCD4X_READ_DELAY_MS          1U
#define SCD4X_SELFTEST_DELAY_MS      10000U

/* CRC8：poly 0x31, init 0xFF, MSB first */
#define SCD4X_CRC_POLY               0x31U
#define SCD4X_CRC_INIT               0xFFU

/* 转换系数 */
#define SCD4X_TEMP_SCALE             175.0f
#define SCD4X_TEMP_OFFSET            (-45.0f)
#define SCD4X_HUMIDITY_SCALE         100.0f
#define SCD4X_RAW_MAX                65536.0f

#endif /* SCD4X_REGISTER_H */
