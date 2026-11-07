#ifndef DS3231_REGISTER_H
#define DS3231_REGISTER_H

/* DS3231 高精度 I2C 实时时钟寄存器定义（地址 0x68） */

#define DS3231_DEFAULT_ADDRESS      0x68U
#define DS3231_ADDRESS_MASK         0x7FU

/* 时间寄存器（BCD） */
#define DS3231_REG_SECONDS          0x00U
#define DS3231_REG_MINUTES          0x01U
#define DS3231_REG_HOURS            0x02U
#define DS3231_REG_DAY              0x03U
#define DS3231_REG_DATE             0x04U
#define DS3231_REG_MONTH            0x05U   /* bit7 为世纪位 */
#define DS3231_REG_YEAR             0x06U

/* 闹钟 1：秒/分/时/日 */
#define DS3231_REG_ALARM1_SECONDS   0x07U
#define DS3231_REG_ALARM1_MINUTES   0x08U
#define DS3231_REG_ALARM1_HOURS     0x09U
#define DS3231_REG_ALARM1_DAY       0x0AU
/* 闹钟 2：分/时/日 */
#define DS3231_REG_ALARM2_MINUTES   0x0BU
#define DS3231_REG_ALARM2_HOURS     0x0CU
#define DS3231_REG_ALARM2_DAY       0x0DU

#define DS3231_REG_CONTROL          0x0EU
#define DS3231_REG_STATUS           0x0FU
#define DS3231_REG_AGING            0x10U
#define DS3231_REG_TEMP_MSB         0x11U
#define DS3231_REG_TEMP_LSB         0x12U

/* 秒寄存器 CH 位 */
#define DS3231_SEC_CLOCK_HALT       0x80U

/* 小时寄存器 */
#define DS3231_HOUR_12H_MODE        0x40U
#define DS3231_HOUR_PM              0x20U
#define DS3231_HOUR_12H_MASK        0x1FU
#define DS3231_HOUR_24H_MASK        0x3FU

/* 月寄存器世纪位 */
#define DS3231_MONTH_CENTURY        0x80U
#define DS3231_MONTH_MASK           0x1FU

/* 控制寄存器 */
#define DS3231_CTRL_EOSC            0x80U
#define DS3231_CTRL_BBSQW           0x40U
#define DS3231_CTRL_CONV            0x20U   /* 强制一次温度转换 */
#define DS3231_CTRL_RS2             0x10U
#define DS3231_CTRL_RS1             0x08U
#define DS3231_CTRL_INTCN           0x04U
#define DS3231_CTRL_A2IE            0x02U
#define DS3231_CTRL_A1IE            0x01U

/* 状态寄存器 */
#define DS3231_STATUS_OSF           0x80U   /* 振荡器停振标志 */
#define DS3231_STATUS_EN32KHZ       0x08U
#define DS3231_STATUS_BSY           0x04U
#define DS3231_STATUS_A2F           0x02U
#define DS3231_STATUS_A1F           0x01U

/* 方波频率（RS2:RS1） */
#define DS3231_SQW_1HZ              0x00U
#define DS3231_SQW_1024HZ           0x01U
#define DS3231_SQW_4096HZ           0x02U
#define DS3231_SQW_8192HZ           0x03U
#define DS3231_SQW_DISABLE          0xFFU

/* 温度转换等待（强制转换典型 125ms，保守 200ms） */
#define DS3231_CONV_DELAY_MS        200U
#define DS3231_POWER_ON_DELAY_MS    10U
#define DS3231_LSB_CELSIUS          0.25f

/* 掩码 */
#define DS3231_DATE_MASK            0x3FU
#define DS3231_WEEKDAY_MASK         0x07U

#endif /* DS3231_REGISTER_H */
