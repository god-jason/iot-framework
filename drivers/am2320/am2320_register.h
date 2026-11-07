#ifndef AM2320_REGISTER_H
#define AM2320_REGISTER_H

/*
 * AM2320 命令与时序定义（I2C 地址 0x5C）。
 *
 * 协议类似 DHT 系（功能码 + 长度 + 数据 + CRC16/Modbus），但走 I2C：
 *   1) 唤醒：器件平时处于休眠，需先发送一次寻址事务，然后等待 >= 800us
 *   2) 发送 0x03 0x00 0x04（读功能码，起始寄存器 0，长度 4）
 *   3) 等待 >= 1.5ms 后读回 8 字节
 *
 * 返回帧格式：
 *   [0]=0x03 [1]=0x04 [2..3]=湿度 [4..5]=温度 [6]=CRC 低 [7]=CRC 高
 * 温度 bit15 为符号位，数值单位 0.1℃；湿度单位 0.1%RH。
 */

#define AM2320_DEFAULT_ADDRESS      0x5CU
#define AM2320_ADDRESS_ALT          0x5DU

/* 功能码与寄存器 */
#define AM2320_FUNC_READ            0x03U
#define AM2320_REG_START            0x0000U
#define AM2320_REG_COUNT            0x0004U

/* 帧长度 */
#define AM2320_FRAME_LEN            8U

/* 时序（ms / us） */
#define AM2320_WAKE_DELAY_MS        2U      /* 唤醒后等待（手册要求 >= 0.8ms） */
#define AM2320_READ_DELAY_MS        2U      /* 命令后等待（手册要求 >= 1.5ms） */

/* 换算：数值为 0.1 单位 */
#define AM2320_SCALE                10.0f

/* 温度符号位（bit15）与绝对值掩码 */
#define AM2320_TEMP_SIGN            0x8000U
#define AM2320_TEMP_MASK            0x7FFFU

/* 器件 ID（读 0x0B 起的 2 字节，旧固件可能不支持） */
#define AM2320_REG_ID               0x0BU
#define AM2320_DEVICE_ID            0x01B0U

#endif /* AM2320_REGISTER_H */
