#ifndef DS1307_REGISTER_H
#define DS1307_REGISTER_H

/* DS1307 I2C 实时时钟寄存器定义（地址 0x68） */

#define DS1307_DEFAULT_ADDRESS      0x68U
#define DS1307_ADDRESS_MASK         0x7FU

/* 时钟寄存器（BCD） */
#define DS1307_REG_SECONDS          0x00U
#define DS1307_REG_MINUTES          0x01U
#define DS1307_REG_HOURS            0x02U
#define DS1307_REG_DAY              0x03U   /* 星期 1-7 */
#define DS1307_REG_DATE             0x04U
#define DS1307_REG_MONTH            0x05U
#define DS1307_REG_YEAR             0x06U
#define DS1307_REG_CONTROL          0x07U
#define DS1307_REG_RAM_BASE         0x08U
#define DS1307_RAM_SIZE             56U     /* 0x08 - 0x3F */

/* 秒寄存器 bit7：时钟停止位 */
#define DS1307_SEC_CLOCK_HALT       0x80U

/* 小时寄存器：bit6 12/24 制选择，bit5 PM */
#define DS1307_HOUR_12H_MODE        0x40U
#define DS1307_HOUR_PM              0x20U
#define DS1307_HOUR_12H_MASK        0x1FU
#define DS1307_HOUR_24H_MASK        0x3FU

/* 控制寄存器 */
#define DS1307_CTRL_OUT             0x80U
#define DS1307_CTRL_SQWE            0x10U
#define DS1307_CTRL_RS_MASK         0x03U

/* 方波输出频率（RS1:RS0） */
#define DS1307_SQW_1HZ              0x00U
#define DS1307_SQW_4096HZ           0x01U
#define DS1307_SQW_8192HZ           0x02U
#define DS1307_SQW_32768HZ          0x03U
#define DS1307_SQW_DISABLE          0xFFU   /* 伪值：关闭 SQWE */

/* 掩码 */
#define DS1307_MONTH_MASK           0x1FU
#define DS1307_DATE_MASK            0x3FU
#define DS1307_WEEKDAY_MASK         0x07U

/* 上电稳定时间 */
#define DS1307_POWER_ON_DELAY_MS    10U

#endif /* DS1307_REGISTER_H */
