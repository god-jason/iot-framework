#ifndef DS1302_REGISTER_H
#define DS1302_REGISTER_H

/*
 * DS1302 涓流充电时钟芯片寄存器与命令定义。
 *
 * 命令字与数据均 LSB first；时钟寄存器为 BCD 编码。
 * 写操作前必须清除写保护位（0x8E 写 0x00）。
 */

#define DS1302_DEFAULT_ADDRESS      0x00U   /* 预留：DS1302 无 I2C 地址 */

/* 时钟寄存器（写地址 = 读地址 | 0x01） */
#define DS1302_REG_SECONDS          0x80U
#define DS1302_REG_MINUTES          0x82U
#define DS1302_REG_HOURS            0x84U
#define DS1302_REG_DATE             0x86U
#define DS1302_REG_MONTH            0x88U
#define DS1302_REG_DAY              0x8AU
#define DS1302_REG_YEAR             0x8CU
#define DS1302_REG_WRITE_PROTECT    0x8EU
#define DS1302_REG_TRICKLE_CHARGE   0x90U

/* 突发模式 */
#define DS1302_CMD_CLOCK_BURST      0xBEU
#define DS1302_CMD_RAM_BURST        0xFEU

/* 时钟寄存器位 */
#define DS1302_SEC_CLOCK_HALT       0x80U   /* 1: 振荡器停止 */
#define DS1302_HOUR_12H_MODE        0x80U   /* 1: 12 小时制 */
#define DS1302_HOUR_PM              0x20U   /* 12 小时制下 1: PM */
#define DS1302_HOUR_12H_MASK        0x1FU
#define DS1302_HOUR_24H_MASK        0x3FU
#define DS1302_MONTH_MASK           0x1FU
#define DS1302_DATE_MASK            0x3FU
#define DS1302_WEEKDAY_MASK         0x07U
#define DS1302_WP_ENABLE            0x80U   /* 写保护使能 */
#define DS1302_WP_DISABLE           0x00U

/* 涓流充电寄存器取值（TCS | DS | RS） */
#define DS1302_TRICKLE_DISABLE      0x00U
#define DS1302_TRICKLE_1D_2K        0xA5U   /* 1 二极管 + 2kΩ */
#define DS1302_TRICKLE_1D_4K        0xA6U
#define DS1302_TRICKLE_1D_8K        0xA7U
#define DS1302_TRICKLE_2D_2K        0xA9U   /* 2 二极管 + 2kΩ */
#define DS1302_TRICKLE_2D_4K        0xAAU
#define DS1302_TRICKLE_2D_8K        0xABU

/* 内部 RAM：31 x 8 bit，命令地址基址（每字节占 2 个地址） */
#define DS1302_RAM_BASE             0xC0U
#define DS1302_RAM_SIZE             31U

/* 三线时序（微秒），DS1302 最高 SCLK 2MHz，保守取 1us 半周期 */
#define DS1302_TICK_US              1U
#define DS1302_CE_SETTLE_US         4U

#endif /* DS1302_REGISTER_H */
