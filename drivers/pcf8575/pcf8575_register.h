#ifndef PCF8575_REGISTER_H
#define PCF8575_REGISTER_H

/*
 * PCF8575 / PCF8575C 16 位准双向 IO 扩展（I2C，地址 0x20~0x27）。
 *
 * 器件无寄存器地址：读为 2 字节（P0、P1），写为 2 字节（P0、P1），
 * 每个端口 8 位，写 1 后引脚由上拉保持高电平，可直接作为输入读取。
 */

#define PCF8575_DEFAULT_ADDRESS     0x20U
#define PCF8575_ADDRESS_MAX         0x27U

/* 端口位数 */
#define PCF8575_PORT_BITS           8U
#define PCF8575_PIN_COUNT           16U
#define PCF8575_PORT_COUNT          2U

/* 上电/初始化时全部写 1，使引脚处于高电平（输入模式） */
#define PCF8575_ALL_HIGH            0xFFFFU

/* 一次读写的字节数 */
#define PCF8575_FRAME_LEN           2U

#endif /* PCF8575_REGISTER_H */
