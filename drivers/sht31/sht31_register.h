#ifndef SHT31_REGISTER_H
#define SHT31_REGISTER_H

/*
 * SHT31 命令定义（SHT3x 命令集，命令式 I2C，无寄存器地址）。
 *
 * 单次测量 -> 等待转换 -> 读 6 字节：
 *   [0..1] 温度  [2] CRC  [3..4] 湿度  [5] CRC
 */

#define SHT31_DEFAULT_ADDRESS         0x44U
#define SHT31_ADDRESS_ALT             0x45U

/* 单次测量，clock stretching 关闭，高/中/低重复性 */
#define SHT31_CMD_MEASURE_HIGH        0x2C06U
#define SHT31_CMD_MEASURE_MEDIUM      0x2C0DU
#define SHT31_CMD_MEASURE_LOW         0x2C10U

/* 周期测量（1 次/秒） */
#define SHT31_CMD_PERIODIC_1MPS_HIGH  0x2130U
#define SHT31_CMD_PERIODIC_1MPS_MED   0x2136U
#define SHT31_CMD_PERIODIC_1MPS_LOW   0x213DU

/* 其它命令 */
#define SHT31_CMD_SOFT_RESET          0x30A2U
#define SHT31_CMD_HEATER_ENABLE       0x306DU
#define SHT31_CMD_HEATER_DISABLE      0x3066U
#define SHT31_CMD_READ_STATUS         0xF32DU
#define SHT31_CMD_BREAK               0x3093U

/* 状态寄存器位 */
#define SHT31_STATUS_ALERT_PENDING    0x8000U
#define SHT31_STATUS_HEATER_ON        0x2000U
#define SHT31_STATUS_RH_ALERT         0x0800U
#define SHT31_STATUS_T_ALERT          0x0400U
#define SHT31_STATUS_RESET_DETECTED   0x0010U
#define SHT31_STATUS_CMD_FAILED       0x0002U
#define SHT31_STATUS_CRC_FAILED       0x0001U

/* 时序（ms）：高重复性 15ms，中 6ms，低 4ms，复位 2ms */
#define SHT31_RESET_DELAY_MS          2U
#define SHT31_MEASURE_DELAY_MS        20U
#define SHT31_MEASURE_DELAY_MED_MS    8U
#define SHT31_MEASURE_DELAY_LOW_MS    5U

/* 换算：T = -45 + 175 * raw / 65535，RH = 100 * raw / 65535 */
#define SHT31_RAW_MAX                 65535.0f
#define SHT31_TEMP_SCALE              175.0f
#define SHT31_TEMP_OFFSET             (-45.0f)
#define SHT31_HUMIDITY_SCALE          100.0f

/* CRC8：poly 0x31, init 0xFF */
#define SHT31_CRC_POLY                0x31U
#define SHT31_CRC_INIT                0xFFU

#endif /* SHT31_REGISTER_H */
