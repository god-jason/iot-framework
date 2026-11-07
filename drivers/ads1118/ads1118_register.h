#ifndef ADS1118_REGISTER_H
#define ADS1118_REGISTER_H

/*
 * ADS1118 16 位 4 通道 Δ-Σ ADC（SPI）配置位定义。
 *
 * 器件无地址，配置只有 16 位：
 *   15 SS | 14:12 MUX | 11:9 PGA | 8 MODE | 7:5 DR | 4 TS_MODE | 3 PULL_UP_EN | 2:1 NOP | 0 保留(1)
 *
 * 一次 32 位传输中：前 16 位写配置，后 16 位写 NOP(0x0000) 以读回配置；
 * 同时前 16 位读出的是上一次转换结果。
 */

/* 配置位 */
#define ADS1118_CFG_SS              0x8000U /* 1: 启动单次转换 */
#define ADS1118_CFG_MUX_SHIFT       12U
#define ADS1118_CFG_MUX_MASK        0x7000U
#define ADS1118_CFG_PGA_SHIFT       9U
#define ADS1118_CFG_PGA_MASK        0x0E00U
#define ADS1118_CFG_MODE_SINGLE     0x0100U /* 1: 单次转换（转换后进入掉电） */
#define ADS1118_CFG_DR_SHIFT        5U
#define ADS1118_CFG_DR_MASK         0x00E0U
#define ADS1118_CFG_TS_MODE         0x0010U /* 1: 内部温度传感器 */
#define ADS1118_CFG_PULL_UP_EN      0x0008U
#define ADS1118_CFG_NOP_MASK        0x0006U
#define ADS1118_CFG_RESERVED        0x0001U /* 固定为 1，可用于有效性校验 */

/* 复位默认配置 */
#define ADS1118_CFG_DEFAULT         0x058BU

/* 内部温度传感器：0.03125 ℃/LSB，14 位有效（结果左对齐） */
#define ADS1118_TEMP_LSB_C          0.03125f
#define ADS1118_TEMP_SHIFT          2U

/* 单次转换轮询余量（ms） */
#define ADS1118_CONV_EXTRA_MS       20U

/* PGA 满量程（mV） */
#define ADS1118_FS_6V144_MV         6144U
#define ADS1118_FS_4V096_MV         4096U
#define ADS1118_FS_2V048_MV         2048U
#define ADS1118_FS_1V024_MV         1024U
#define ADS1118_FS_0V512_MV         512U
#define ADS1118_FS_0V256_MV         256U

#endif /* ADS1118_REGISTER_H */
