#ifndef SHT30_REGISTER_H
#define SHT30_REGISTER_H

/* SHT30 / SHT31 / SHT35 命令定义（无寄存器地址，命令式接口） */

#define SHT30_DEFAULT_ADDRESS       0x44U
#define SHT30_ADDRESS_ALT           0x45U

/* 单次测量，Clock Stretching 关闭 */
#define SHT30_CMD_MEASURE_HIGH      0x2C06U  /* 高重复性 */
#define SHT30_CMD_MEASURE_MEDIUM    0x2C0DU  /* 中重复性 */
#define SHT30_CMD_MEASURE_LOW       0x2C10U  /* 低重复性 */

/* 周期测量 */
#define SHT30_CMD_PERIODIC_1MPS_HIGH  0x2130U
#define SHT30_CMD_PERIODIC_1MPS_MED   0x2136U

/* 其他命令 */
#define SHT30_CMD_SOFT_RESET        0x30A2U
#define SHT30_CMD_HEATER_ENABLE     0x306DU
#define SHT30_CMD_HEATER_DISABLE    0x3066U
#define SHT30_CMD_BREAK             0x3093U

/* 时序（ms） */
#define SHT30_RESET_DELAY_MS        2U
#define SHT30_MEASURE_DELAY_MS      20U

/* 转换系数 */
#define SHT30_TEMP_SCALE            175.0f
#define SHT30_TEMP_OFFSET           (-45.0f)
#define SHT30_HUMIDITY_SCALE        100.0f
#define SHT30_RAW_MAX               65535.0f

/* CRC8 参数：poly 0x31, init 0xFF */
#define SHT30_CRC_POLY              0x31U
#define SHT30_CRC_INIT              0xFFU

#endif /* SHT30_REGISTER_H */
