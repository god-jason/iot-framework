#ifndef SHT35_REGISTER_H
#define SHT35_REGISTER_H

/*
 * SHT35 命令定义（与 SHT3x 命令集完全兼容，命令式 I2C）。
 *
 * 与 SHT31 的差别仅在精度等级（±1.5%RH / ±0.1℃），命令与时序一致。
 * 读回 6 字节：[0..1] 温度 [2] CRC [3..4] 湿度 [5] CRC
 */

#define SHT35_DEFAULT_ADDRESS         0x44U
#define SHT35_ADDRESS_ALT             0x45U

/* 单次测量，clock stretching 关闭 */
#define SHT35_CMD_MEASURE_HIGH        0x2C06U
#define SHT35_CMD_MEASURE_MEDIUM      0x2C0DU
#define SHT35_CMD_MEASURE_LOW         0x2C10U

/* 周期测量，1 次/秒 */
#define SHT35_CMD_PERIODIC_1MPS_HIGH  0x2130U
#define SHT35_CMD_PERIODIC_1MPS_MED   0x2136U

/* 其它命令 */
#define SHT35_CMD_SOFT_RESET          0x30A2U
#define SHT35_CMD_HEATER_ENABLE       0x306DU
#define SHT35_CMD_HEATER_DISABLE      0x3066U
#define SHT35_CMD_READ_STATUS         0xF32DU
#define SHT35_CMD_BREAK               0x3093U

/* 状态寄存器位 */
#define SHT35_STATUS_ALERT_PENDING    0x8000U
#define SHT35_STATUS_HEATER_ON        0x2000U
#define SHT35_STATUS_RESET_DETECTED   0x0010U
#define SHT35_STATUS_CMD_FAILED       0x0002U
#define SHT35_STATUS_CRC_FAILED       0x0001U

/* 时序（ms） */
#define SHT35_RESET_DELAY_MS          2U
#define SHT35_MEASURE_DELAY_MS        20U
#define SHT35_MEASURE_DELAY_MED_MS    8U
#define SHT35_MEASURE_DELAY_LOW_MS    5U

/* 换算：T = -45 + 175 * raw / 65535，RH = 100 * raw / 65535 */
#define SHT35_RAW_MAX                 65535.0f
#define SHT35_TEMP_SCALE              175.0f
#define SHT35_TEMP_OFFSET             (-45.0f)
#define SHT35_HUMIDITY_SCALE          100.0f

/* CRC8：poly 0x31, init 0xFF */
#define SHT35_CRC_POLY                0x31U
#define SHT35_CRC_INIT                0xFFU

#endif /* SHT35_REGISTER_H */
