#ifndef MAX31865_REGISTER_H
#define MAX31865_REGISTER_H

/* MAX31865 RTD 转换器寄存器定义（SPI，PT100 / PT1000） */

/* SPI 帧格式：bit7 = 1 读 / 0 写，bit6:0 = 寄存器地址 */
#define MAX31865_SPI_READ           0x80U
#define MAX31865_SPI_WRITE          0x00U

#define MAX31865_REG_CONFIG         0x00U
#define MAX31865_REG_RTD_MSB        0x01U
#define MAX31865_REG_RTD_LSB        0x02U
#define MAX31865_REG_FAULT_HIGH_MSB 0x03U
#define MAX31865_REG_FAULT_HIGH_LSB 0x04U
#define MAX31865_REG_FAULT_LOW_MSB  0x05U
#define MAX31865_REG_FAULT_LOW_LSB  0x06U
#define MAX31865_REG_FAULT_STATUS   0x07U

/* 配置寄存器位 */
#define MAX31865_CONFIG_VBIAS       0x80U   /* 使能 RTD 偏置电压 */
#define MAX31865_CONFIG_MODE_AUTO   0x00U   /* 自动转换（典型 60Hz/50Hz 周期） */
#define MAX31865_CONFIG_MODE_1SHOT  0x40U   /* 单次转换模式 */
#define MAX31865_CONFIG_1SHOT       0x20U   /* 触发一次转换（自动清零） */
#define MAX31865_CONFIG_3WIRE       0x10U   /* 1: 3 线；0: 2/4 线 */
#define MAX31865_CONFIG_FAULT_CLEAR 0x02U   /* 写 1 清除故障状态 */
#define MAX31865_CONFIG_FILTER_50HZ 0x00U   /* bit0=0: 50Hz 陷波 */
#define MAX31865_CONFIG_FILTER_60HZ 0x01U   /* bit0=1: 60Hz 陷波 */

/* RTD LSB 的 bit0 为故障标志，高 15 位为 ADC 码 */
#define MAX31865_RTD_FAULT_BIT      0x0001U
#define MAX31865_RTD_FULL_SCALE     32768.0f

/* 故障状态位（bit7 - bit2，bit1:0 保留） */
#define MAX31865_FAULT_RTD_HIGH     0x80U   /* RTD 超过上限阈值 */
#define MAX31865_FAULT_RTD_LOW      0x40U   /* RTD 低于下限阈值 */
#define MAX31865_FAULT_REFIN_HIGH   0x20U   /* REFIN- > 0.85 x VBIAS */
#define MAX31865_FAULT_REFIN_LOW    0x10U   /* REFIN- < 0.85 x VBIAS（FORCE- 开路） */
#define MAX31865_FAULT_RTDIN_LOW    0x08U   /* RTDIN- < 0.85 x VBIAS（FORCE- 开路） */
#define MAX31865_FAULT_OVUV         0x04U   /* 过压 / 欠压 */

/* Callendar - Van Dusen 系数（IEC 60751） */
#define MAX31865_CVD_A              3.90830e-3f
#define MAX31865_CVD_B              (-5.77500e-7f)
#define MAX31865_CVD_C              (-4.18301e-12f)

/* 时序（ms） */
#define MAX31865_CONV_1SHOT_MS      70U    /* 50Hz 滤波单次转换 */
#define MAX31865_CONV_AUTO_MS       100U   /* 自动模式首次转换等待 */
#define MAX31865_POWER_ON_MS        10U

/* 默认器件参数 */
#define MAX31865_DEFAULT_RREF       430.0f  /* PT100 常用 430Ω（也有 400Ω/4.3kΩ） */
#define MAX31865_DEFAULT_R0         100.0f

#endif /* MAX31865_REGISTER_H */
