#ifndef BMP390_REGISTER_H
#define BMP390_REGISTER_H

/*
 * BMP390 寄存器 / 校准系数 / 量化位定义。
 *
 * BMP390 与 BMP388 寄存器映射、校准系数布局和补偿公式完全一致，
 * 区别仅在于 chip id（0x60）与电气指标。数据来源：Bosch BMP390 datasheet。
 *   - 数据区 0x04..0x09（压力 24 位 + 温度 24 位，小端，XLSB 在前）
 *   - 校准系数 0x31..0x44（20 字节，小端）
 *
 * 校准系数在芯片内为定点量化值，实际值 = 原始值 / 2^N（N 见下表，
 * N 取负值表示乘 2^|N|），即 BMP390_CAL_*_SCALE。
 */

#define BMP390_DEFAULT_ADDRESS      0x76U
#define BMP390_ADDRESS_ALT          0x77U

#define BMP390_REG_CHIP_ID          0x00U
#define BMP390_REG_ERR              0x02U
#define BMP390_REG_STATUS           0x03U
#define BMP390_REG_DATA_0           0x04U   /* 6 字节：P XLSB/LSB/MSB, T XLSB/LSB/MSB */
#define BMP390_REG_INT_CTRL         0x19U
#define BMP390_REG_IF_CONF          0x1AU
#define BMP390_REG_PWR_CTRL         0x1BU
#define BMP390_REG_OSR              0x1CU
#define BMP390_REG_ODR              0x1DU
#define BMP390_REG_CONFIG           0x1FU
#define BMP390_REG_CALIB            0x31U   /* 20 字节 */
#define BMP390_REG_CMD              0x7EU

#define BMP390_CHIP_ID              0x60U
#define BMP390_CMD_SOFT_RESET       0xB6U

/* pwr_ctrl：bit0 press_en，bit1 temp_en，bit5:4 mode（0x3 = normal） */
#define BMP390_PWR_CTRL_NORMAL      0x33U
/* osr：bit5:3 osr_t，bit2:0 osr_p；0x1 = x2 ... 0x5 = x32 */
#define BMP390_OSR_T_X2             0x01U
#define BMP390_OSR_P_X8             0x03U
#define BMP390_OSR_DEFAULT          ((uint8_t)((BMP390_OSR_T_X2 << 3) | BMP390_OSR_P_X8))
#define BMP390_CONFIG_DEFAULT       0x00U   /* IIR 滤波关闭 */

#define BMP390_RESET_DELAY_MS       3U

#define BMP390_CALIB_DATA_SIZE      20U
#define BMP390_DATA_SIZE            6U

/* 系数量化（实际值 = 原始值 / 2^N，N 可为负） */
#define BMP390_CAL_T1_SCALE         256.0        /* 2^-8  */
#define BMP390_CAL_T2_SCALE         9.313225746e-10   /* 2^-30 */
#define BMP390_CAL_T3_SCALE         3.552713679e-15   /* 2^-48 */
#define BMP390_CAL_P1_SCALE         9.536743164e-07   /* 2^-20 */
#define BMP390_CAL_P2_SCALE         1.862645149e-09   /* 2^-29 */
#define BMP390_CAL_P3_SCALE         2.328306437e-10   /* 2^-32 */
#define BMP390_CAL_P4_SCALE         7.275957614e-12   /* 2^-37 */
#define BMP390_CAL_P5_SCALE         256.0        /* 2^-8  */
#define BMP390_CAL_P6_SCALE         2.441406250e-04   /* 2^-12 */
#define BMP390_CAL_P7_SCALE         7.629394531e-06   /* 2^-17 */
#define BMP390_CAL_P8_SCALE         5.960464478e-08   /* 2^-24 */
#define BMP390_CAL_P9_SCALE         1.490116119e-08   /* 2^-26 */
#define BMP390_CAL_P10_SCALE        2.910383045e-11   /* 2^-35 */
#define BMP390_CAL_P11_SCALE        5.684341886e-14   /* 2^-44 */

/* 输出合理区间 */
#define BMP390_PRESSURE_MIN_PA      30000.0f
#define BMP390_PRESSURE_MAX_PA      110000.0f

#endif /* BMP390_REGISTER_H */
