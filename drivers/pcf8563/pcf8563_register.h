#ifndef PCF8563_REGISTER_H
#define PCF8563_REGISTER_H

/* PCF8563 I2C 实时时钟 / 日历寄存器定义（地址 0x51） */

#define PCF8563_DEFAULT_ADDRESS     0x51U
#define PCF8563_ADDRESS_MASK        0x7FU

/* 寄存器 */
#define PCF8563_REG_CONTROL1        0x00U
#define PCF8563_REG_CONTROL2        0x01U
#define PCF8563_REG_SECONDS         0x02U   /* bit7 = VL（电压低标志） */
#define PCF8563_REG_MINUTES         0x03U
#define PCF8563_REG_HOURS           0x04U
#define PCF8563_REG_DAYS            0x05U
#define PCF8563_REG_WEEKDAYS        0x06U
#define PCF8563_REG_CENTURY_MONTHS  0x07U   /* bit7 = 世纪位 */
#define PCF8563_REG_YEARS           0x08U
#define PCF8563_REG_MINUTE_ALARM    0x09U
#define PCF8563_REG_HOUR_ALARM      0x0AU
#define PCF8563_REG_DAY_ALARM       0x0BU
#define PCF8563_REG_WEEKDAY_ALARM   0x0CU
#define PCF8563_REG_CLKOUT          0x0DU
#define PCF8563_REG_TIMER_CONTROL   0x0EU
#define PCF8563_REG_TIMER           0x0FU

/* CONTROL_1 */
#define PCF8563_CTRL1_TEST          0x80U
#define PCF8563_CTRL1_STOP          0x20U   /* 1: 停止计数 */
#define PCF8563_CTRL1_TESTC         0x08U

/* CONTROL_2 */
#define PCF8563_CTRL2_TI_TP         0x10U
#define PCF8563_CTRL2_AF            0x08U
#define PCF8563_CTRL2_TF            0x04U
#define PCF8563_CTRL2_AIE           0x02U
#define PCF8563_CTRL2_TIE           0x01U

/* 时间寄存器位 */
#define PCF8563_VL_FLAG             0x80U
#define PCF8563_CENTURY_BIT         0x80U
#define PCF8563_MONTH_MASK          0x1FU
#define PCF8563_DAY_MASK            0x3FU
#define PCF8563_WEEKDAY_MASK        0x07U
#define PCF8563_HOUR_MASK           0x3FU
#define PCF8563_MINUTE_MASK         0x7FU

/* CLKOUT 控制：FE | FD1 FD0 */
#define PCF8563_CLKOUT_ENABLE       0x80U
#define PCF8563_CLKOUT_32768HZ      0x00U
#define PCF8563_CLKOUT_1024HZ       0x01U
#define PCF8563_CLKOUT_32HZ         0x02U
#define PCF8563_CLKOUT_1HZ          0x03U

#define PCF8563_POWER_ON_DELAY_MS   10U

#endif /* PCF8563_REGISTER_H */
