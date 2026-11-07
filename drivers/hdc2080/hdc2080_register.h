#ifndef HDC2080_REGISTER_H
#define HDC2080_REGISTER_H

/*
 * HDC2080 寄存器定义（I2C，7 位地址 0x41）。
 *
 * 注意数据字节序：温度/湿度寄存器均为低字节在前（LSB 在低地址）。
 *   0x00 TEMP_LSB, 0x01 TEMP_MSB
 *   0x02 HUMID_LSB, 0x03 HUMID_MSB
 * 一次突发读 4 字节即可同时取到温度与湿度。
 */

#define HDC2080_DEFAULT_ADDRESS        0x41U
#define HDC2080_ADDRESS_ALT            0x40U

/* 数据寄存器 */
#define HDC2080_REG_TEMP_LSB           0x00U
#define HDC2080_REG_TEMP_MSB           0x01U
#define HDC2080_REG_HUMID_LSB          0x02U
#define HDC2080_REG_HUMID_MSB          0x03U

/* 配置与测量寄存器 */
#define HDC2080_REG_CONFIG             0x0FU
#define HDC2080_REG_MEASUREMENT_CONFIG 0x10U

/* 标识寄存器（低字节在前） */
#define HDC2080_REG_MANUFACTURER_ID_L  0xFBU
#define HDC2080_REG_MANUFACTURER_ID_H  0xFCU
#define HDC2080_REG_DEVICE_ID_L        0xFDU
#define HDC2080_REG_DEVICE_ID_H        0xFEU

/* 标识值：厂商 'TI'，器件 ID 0x07D0（HDC2080/HDC2010 共用） */
#define HDC2080_MANUFACTURER_ID        0x5449U
#define HDC2080_DEVICE_ID              0x07D0U

/* CONFIG 寄存器位 */
#define HDC2080_CONFIG_MEAS_TRIG       0x01U   /* 1: 启动一次测量（自清零） */
#define HDC2080_CONFIG_HEATER          0x08U
#define HDC2080_CONFIG_SOFT_RESET      0x80U   /* 软复位（自清零） */
/* MEAS_MODE = 00：温度 + 湿度顺序测量 */
#define HDC2080_MEAS_MODE_TEMP_HUMID   0x00U
/* 分辨率：0 = 14 位（温度/湿度均 14 位） */
#define HDC2080_MEAS_CONFIG_14BIT      0x00U

/* 时序（ms）：14 位转换最大约 6.6ms */
#define HDC2080_RESET_DELAY_MS         15U
#define HDC2080_CONVERT_DELAY_MS       10U

/* 换算：T = raw / 2^16 * 165 - 40，RH = raw / 2^16 * 100 */
#define HDC2080_RAW_MAX                65536.0f
#define HDC2080_TEMP_SCALE             165.0f
#define HDC2080_TEMP_OFFSET            (-40.0f)
#define HDC2080_HUMIDITY_SCALE         100.0f

#endif /* HDC2080_REGISTER_H */
