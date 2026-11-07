#ifndef STS3X_REGISTER_H
#define STS3X_REGISTER_H

/* Sensirion STS3x（STS30/STS31/STS32/STS35）温度传感器命令定义 */

#define STS3X_ADDRESS_DEFAULT           0x4AU
#define STS3X_ADDRESS_ALT               0x4BU

/* 单次测量命令（不带时钟拉伸，主机延时后读取） */
#define STS3X_CMD_MEASURE_HIGH          0x2400U  /* 高重复性 */
#define STS3X_CMD_MEASURE_MEDIUM        0x240BU  /* 中重复性 */
#define STS3X_CMD_MEASURE_LOW           0x2416U  /* 低重复性 */

/* 周期测量命令（mps=0.5/1/2/4/10，高重复性） */
#define STS3X_CMD_PERIODIC_0_5_HIGH     0x2032U
#define STS3X_CMD_PERIODIC_1_HIGH       0x2130U
#define STS3X_CMD_PERIODIC_2_HIGH       0x2236U
#define STS3X_CMD_PERIODIC_4_HIGH       0x2334U
#define STS3X_CMD_PERIODIC_10_HIGH      0x2737U
#define STS3X_CMD_FETCH_DATA            0xE000U
#define STS3X_CMD_BREAK                 0x3093U

/* 系统命令 */
#define STS3X_CMD_SOFT_RESET            0x30A2U
#define STS3X_CMD_READ_STATUS           0xF32DU
#define STS3X_CMD_CLEAR_STATUS          0x3041U

/* 换算 */
#define STS3X_TEMP_OFFSET_C             (-45.0f)
#define STS3X_TEMP_SCALE_C              175.0f
#define STS3X_RAW_MAX                   65535.0f

/* CRC-8：poly 0x31，init 0xFF */
#define STS3X_CRC_POLY                  0x31U
#define STS3X_CRC_INIT                  0xFFU

/* 测量延时（ms） */
#define STS3X_MEASURE_HIGH_MS           16U
#define STS3X_MEASURE_MEDIUM_MS         7U
#define STS3X_MEASURE_LOW_MS            3U
#define STS3X_RESET_DELAY_MS            2U

#endif /* STS3X_REGISTER_H */
