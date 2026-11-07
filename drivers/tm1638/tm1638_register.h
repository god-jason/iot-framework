#ifndef TM1638_REGISTER_H
#define TM1638_REGISTER_H

/*
 * TM1638 三线（STB/CLK/DIO）LED 数码管 + 按键驱动命令定义。
 * 显示寄存器 0x00..0x0F：0x00..0x07 为 8 位数码管（含小数点），
 * 0x08..0x0F 为 8 个独立 LED（bit0 有效）。
 */

#define TM1638_CMD_DATA_AUTO        0x40U   /* 自动地址递增写数据 */
#define TM1638_CMD_DATA_FIXED       0x44U   /* 固定地址写数据 */
#define TM1638_CMD_READ_KEYS        0x42U   /* 读按键 */
#define TM1638_CMD_DISPLAY_OFF      0x80U
#define TM1638_CMD_DISPLAY_ON       0x88U   /* | 亮度 0..7 */
#define TM1638_CMD_ADDR_BASE        0xC0U

#define TM1638_REG_COUNT            16U
#define TM1638_DIGIT_COUNT          8U
#define TM1638_LED_OFFSET           8U
#define TM1638_KEY_BYTES            4U

#define TM1638_BRIGHTNESS_MIN       0U
#define TM1638_BRIGHTNESS_MAX       7U
#define TM1638_BRIGHTNESS_DEFAULT   5U

/* 段码位（数码管寄存器内） */
#define TM1638_SEG_A                0x01U
#define TM1638_SEG_B                0x02U
#define TM1638_SEG_C                0x04U
#define TM1638_SEG_D                0x08U
#define TM1638_SEG_E                0x10U
#define TM1638_SEG_F                0x20U
#define TM1638_SEG_G                0x40U
#define TM1638_SEG_DP               0x80U

#define TM1638_DELAY_US             2U

#endif /* TM1638_REGISTER_H */
