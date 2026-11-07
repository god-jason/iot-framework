#ifndef TTP229_REGISTER_H
#define TTP229_REGISTER_H

/*
 * TTP229 16 键电容触摸芯片。
 *
 * 同一颗芯片有多种封装/配置：
 *   - TTP229-BSF：I2C 接口，从机地址 0x57，读 2 字节（每字节 8 个键）
 *   - TTP229-LSF/BSW：2 线串行接口，SCL 由主机输出，SDO 输出按键位图
 */

/* I2C 版本默认地址（7 位） */
#define TTP229_DEFAULT_ADDRESS      0x57U

/* 键数与帧长 */
#define TTP229_KEY_COUNT            16U
#define TTP229_KEYS_PER_BYTE        8U
#define TTP229_I2C_FRAME_LEN        2U

/* 2 线串行时序（us）：模块允许的时钟频率较低，取 5us 半周期 */
#define TTP229_SCL_HIGH_US          5U
#define TTP229_SCL_LOW_US           5U

/* 等待 SDO 拉低（有按键）的超时（ms） */
#define TTP229_READY_TIMEOUT_MS     50U

/* 上电稳定时间（ms） */
#define TTP229_POWER_ON_MS          10U

#endif /* TTP229_REGISTER_H */
