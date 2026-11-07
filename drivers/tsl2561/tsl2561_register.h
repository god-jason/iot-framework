#ifndef TSL2561_REGISTER_H
#define TSL2561_REGISTER_H

/*
 * TSL2561 环境光传感器寄存器定义（I2C）。
 *
 * 访问任何寄存器时命令字节必须置位 CMD(0x80)；
 * 读取 0x0C~0x0F 时再置位 BLOCK(0x40) 一次性读回两个通道。
 */

#define TSL2561_DEFAULT_ADDRESS     0x39U
#define TSL2561_ADDRESS_LOW         0x29U  /* ADDR 接 GND */
#define TSL2561_ADDRESS_HIGH        0x49U  /* ADDR 接 VDD */

#define TSL2561_CMD                 0x80U
#define TSL2561_CMD_BLOCK           0x40U

#define TSL2561_REG_CONTROL         0x00U
#define TSL2561_REG_TIMING          0x01U
#define TSL2561_REG_ID              0x0AU
#define TSL2561_REG_DATA0LOW        0x0CU
#define TSL2561_REG_DATA1LOW        0x0EU

/* CONTROL 取值 */
#define TSL2561_CONTROL_POWER_OFF   0x00U
#define TSL2561_CONTROL_POWER_ON    0x03U

/* TIMING 位域 */
#define TSL2561_TIMING_GAIN_16X     0x10U  /* 置 1 为 16x，清零为 1x */
#define TSL2561_TIMING_INTEG_MASK   0x03U
#define TSL2561_TIMING_INTEG_13MS   0x00U
#define TSL2561_TIMING_INTEG_101MS  0x01U
#define TSL2561_TIMING_INTEG_402MS  0x02U

/* ID：高 4 位为器件型号（0=TSL2560，1=TSL2561） */
#define TSL2561_ID_PART_MASK        0xF0U
#define TSL2561_ID_PART_TSL2560     0x00U
#define TSL2561_ID_PART_TSL2561     0x10U

/* 积分时间对应的实际时间（ms）与最大计数 */
#define TSL2561_INTEG_13MS_TIME     13.7f
#define TSL2561_INTEG_101MS_TIME    101.0f
#define TSL2561_INTEG_402MS_TIME    402.0f
#define TSL2561_MAX_COUNT           65535U

/* 转换等待时间（ms），比标称积分时间留余量 */
#define TSL2561_DELAY_13MS          15U
#define TSL2561_DELAY_101MS         105U
#define TSL2561_DELAY_402MS         405U

/*
 * lux 转换系数：datasheet 给出的 T / FN / CL 封装近似系数，
 * 实际产品因光学窗口差异需要重新标定。
 */
#define TSL2561_LUX_K1A             0.0315f
#define TSL2561_LUX_K1B             0.0593f
#define TSL2561_LUX_K2A             0.0229f
#define TSL2561_LUX_K2B             0.0291f
#define TSL2561_LUX_K3A             0.0157f
#define TSL2561_LUX_K3B             0.0180f
#define TSL2561_LUX_K4A             0.00338f
#define TSL2561_LUX_K4B             0.00260f
#define TSL2561_LUX_RATIO_1         0.52f
#define TSL2561_LUX_RATIO_2         0.65f
#define TSL2561_LUX_RATIO_3         0.80f
#define TSL2561_LUX_RATIO_4         1.30f

#endif /* TSL2561_REGISTER_H */
