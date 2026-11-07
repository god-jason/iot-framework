#ifndef TM1640_REGISTER_H
#define TM1640_REGISTER_H

/*
 * TM1640 两线（SCLK/DIN）16 段 x 8 位 LED 驱动芯片命令定义。
 * 显示寄存器 0x00..0x0F 对应 16 个 grid，每字节 8 段（bit0=a ... bit7=dp）。
 */

#define TM1640_CMD_DATA_AUTO        0x40U   /* 自动地址递增写数据 */
#define TM1640_CMD_DATA_FIXED       0x44U   /* 固定地址写数据 */
#define TM1640_CMD_ADDR_BASE        0xC0U

#define TM1640_CMD_DISPLAY_OFF      0x80U
#define TM1640_CMD_DISPLAY_ON       0x88U   /* | 亮度 0..7 */

#define TM1640_GRID_COUNT           16U
#define TM1640_BRIGHTNESS_MIN       0U
#define TM1640_BRIGHTNESS_MAX       7U
#define TM1640_BRIGHTNESS_DEFAULT   5U

/* 时序：TM1640 时钟周期约 1us，取 2us 保证裕量 */
#define TM1640_DELAY_US             2U

#endif /* TM1640_REGISTER_H */
