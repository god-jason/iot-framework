#ifndef TM1637_REGISTER_H
#define TM1637_REGISTER_H

/*
 * TM1637 两线（CLK/DIO）7 段数码管驱动芯片命令定义。
 * 段码位序：bit0=a, bit1=b, bit2=c, bit3=d, bit4=e, bit5=f, bit6=g, bit7=dp。
 */

#define TM1637_CMD_DATA_AUTO        0x40U   /* 自动地址递增写数据 */
#define TM1637_CMD_DATA_FIXED       0x44U   /* 固定地址写数据 */
#define TM1637_CMD_READ_KEYS        0x42U   /* 读按键（部分封装支持） */
#define TM1637_CMD_ADDR_BASE        0xC0U   /* 显示寄存器起始地址 */

#define TM1637_CMD_DISPLAY_OFF      0x80U
#define TM1637_CMD_DISPLAY_ON       0x88U   /* | 亮度 0..7 */

#define TM1637_DIGITS_4             4U
#define TM1637_DIGITS_6             6U
#define TM1637_DIGITS_MAX           6U

#define TM1637_BRIGHTNESS_MIN       0U
#define TM1637_BRIGHTNESS_MAX       7U
#define TM1637_BRIGHTNESS_DEFAULT   5U

/* 段码位 */
#define TM1637_SEG_A                0x01U
#define TM1637_SEG_B                0x02U
#define TM1637_SEG_C                0x04U
#define TM1637_SEG_D                0x08U
#define TM1637_SEG_E                0x10U
#define TM1637_SEG_F                0x20U
#define TM1637_SEG_G                0x40U
#define TM1637_SEG_DP               0x80U

/* 位操作延时（us），TM1637 最小 5us */
#define TM1637_DELAY_US             5U

#endif /* TM1637_REGISTER_H */
