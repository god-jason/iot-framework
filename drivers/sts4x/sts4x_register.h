#ifndef STS4X_REGISTER_H
#define STS4X_REGISTER_H

/* Sensirion STS4x（STS40 系列）第 4 代温度传感器命令定义 */

#define STS4X_ADDRESS_DEFAULT           0x46U
#define STS4X_ADDRESS_ALT               0x44U

/* 单次测量（不带时钟拉伸，主机延时后读取） */
#define STS4X_CMD_MEASURE_HIGH          0xFDU  /* 高重复性 */
#define STS4X_CMD_MEASURE_MEDIUM        0xF6U  /* 中重复性 */
#define STS4X_CMD_MEASURE_LOW           0xE0U  /* 低重复性 */

/* 片上加热器脉冲（仅部分型号支持，需按 datasheet 确认） */
#define STS4X_CMD_HEATER_200MW_100MS    0x39U
#define STS4X_CMD_HEATER_200MW_1S       0x32U
#define STS4X_CMD_HEATER_110MW_100MS    0x2FU
#define STS4X_CMD_HEATER_20MW_100MS     0x15U

/* 系统命令 */
#define STS4X_CMD_SOFT_RESET            0x94U
#define STS4X_CMD_READ_SERIAL           0x89U

/* 换算 */
#define STS4X_TEMP_OFFSET_C             (-45.0f)
#define STS4X_TEMP_SCALE_C              175.0f
#define STS4X_RAW_MAX                   65535.0f

/* CRC-8：poly 0x31，init 0xFF */
#define STS4X_CRC_POLY                  0x31U
#define STS4X_CRC_INIT                  0xFFU

/* 测量延时（ms，取 datasheet 最大值的整数上界） */
#define STS4X_MEASURE_HIGH_MS           10U
#define STS4X_MEASURE_MEDIUM_MS         5U
#define STS4X_MEASURE_LOW_MS            3U
#define STS4X_RESET_DELAY_MS            2U

#endif /* STS4X_REGISTER_H */
