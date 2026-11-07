#ifndef BMP388_REGISTER_H
#define BMP388_REGISTER_H

/*
 * BMP388 寄存器 / 校准系数 / 量化位定义。
 *
 * 数据来源：Bosch BMP388 datasheet。
 *   - chip id 0x50
 *   - 数据区 0x04..0x09（压力 24 位 + 温度 24 位，小端，XLSB 在前）
 *   - 校准系数 0x31..0x44（20 字节，小端）
 *
 * 校准系数在芯片内为定点量化值，实际值 = 原始值 / 2^N（N 见下表，
 * N 取负值表示乘 2^|N|），即 BMP388_CAL_*_SCALE。
 */

#define BMP388_DEFAULT_ADDRESS      0x76U
#define BMP388_ADDRESS_ALT          0x77U

#define BMP388_REG_CHIP_ID          0x00U
#define BMP388_REG_ERR              0x02U
#define BMP388_REG_STATUS           0x03U
#define BMP388_REG_DATA_0           0x04U   /* 6 字节：P XLSB/LSB/MSB, T XLSB/LSB/MSB */
#define BMP388_REG_INT_CTRL         0x19U
#define BMP388_REG_IF_CONF          0x1AU
#define BMP388_REG_PWR_CTRL         0x1BU
#define BMP388_REG_OSR              0x1CU
#define BMP388_REG_ODR              0x1DU
#define BMP388_REG_CONFIG           0x1FU
#define BMP388_REG_CALIB            0x31U   /* 20 字节 */
#define BMP388_REG_CMD              0x7EU

#define BMP388_CHIP_ID              0x50U
#define BMP388_CMD_SOFT_RESET       0xB6U

/* pwr_ctrl：bit0 press_en，bit1 temp_en，bit5:4 mode（0x3 = normal） */
#define BMP388_PWR_CTRL_NORMAL      0x33U
/* osr：bit5:3 osr_t，bit2:0 osr_p；0x1 = x2 ... 0x5 = x32 */
#define BMP388_OSR_T_X2             0x01U
#define BMP388_OSR_P_X8             0x03U
#define BMP388_OSR_DEFAULT          ((uint8_t)((BMP388_OSR_T_X2 << 3) | BMP388_OSR_P_X8))
/* config：IIR 滤波关闭 */
#define BMP388_CONFIG_DEFAULT       0x00U

#define BMP388_RESET_DELAY_MS       3U

#define BMP388_CALIB_DATA_SIZE      20U
#define BMP388_DATA_SIZE            6U

/* 系数量化（实际值 = 原始值 / 2^N，N 可为负） */
#define BMP388_CAL_T1_SCALE         256.0        /* 2^-8  */
#define BMP388_CAL_T2_SCALE         9.313225746e-10   /* 2^-30 */
#define BMP388_CAL_T3_SCALE         3.552713679e-15   /* 2^-48 */
#define BMP388_CAL_P1_SCALE         9.536743164e-07   /* 2^-20 */
#define BMP388_CAL_P2_SCALE         1.862645149e-09   /* 2^-29 */
#define BMP388_CAL_P3_SCALE         2.328306437e-10   /* 2^-32 */
#define BMP388_CAL_P4_SCALE         7.275957614e-12   /* 2^-37 */
#define BMP388_CAL_P5_SCALE         256.0        /* 2^-8  */
#define BMP388_CAL_P6_SCALE         2.441406250e-04   /* 2^-12 */
#define BMP388_CAL_P7_SCALE         7.629394531e-06   /* 2^-17 */
#define BMP388_CAL_P8_SCALE         5.960464478e-08   /* 2^-24 */
#define BMP388_CAL_P9_SCALE         1.490116119e-08   /* 2^-26 */
#define BMP388_CAL_P10_SCALE        2.910383045e-11   /* 2^-35 */
#define BMP388_CAL_P11_SCALE        5.684341886e-14   /* 2^-44 */

/* 输出合理区间 */
#define BMP388_PRESSURE_MIN_PA      30000.0f
#define BMP388_PRESSURE_MAX_PA      110000.0f

#endif /* BMP388_REGISTER_H */
