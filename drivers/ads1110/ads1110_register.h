#ifndef ADS1110_REGISTER_H
#define ADS1110_REGISTER_H

/*
 * ADS1110 16 位 Δ-Σ ADC（I2C，地址 0x48~0x4F，由出厂固化）。
 *
 * 只有一个配置字节：
 *   bit7 ST | bit6:5 DR | bit4:3 SC | bit2:0 保留(恒为 0)
 *   ST: 写 1 启动一次转换
 *   DR: 00=15SPS 01=30SPS 10=60SPS 11=240SPS
 *   SC: 00=×1 01=×2 10=×4 11=×8（增益）
 *
 * 读操作固定返回 3 字节：数据高字节、数据低字节、配置字节（回读）。
 */

#define ADS1110_DEFAULT_ADDRESS     0x48U
#define ADS1110_ADDRESS_MAX         0x4FU

/* 配置字节位 */
#define ADS1110_CFG_ST              0x80U
#define ADS1110_CFG_DR_SHIFT        5U
#define ADS1110_CFG_DR_MASK         0x60U
#define ADS1110_CFG_SC_SHIFT        3U
#define ADS1110_CFG_SC_MASK         0x18U
#define ADS1110_CFG_RESERVED_MASK   0x07U   /* 读出必须为 0，可用于器件校验 */

/* 单次转换周期（ms），按数据速率取值 */
#define ADS1110_CONV_MS_15SPS       67U
#define ADS1110_CONV_MS_30SPS       34U
#define ADS1110_CONV_MS_60SPS       17U
#define ADS1110_CONV_MS_240SPS      5U
#define ADS1110_CONV_EXTRA_MS       2U

/* 内部基准 2.048V：满量程 = ±2048mV / 增益 */
#define ADS1110_VREF_MV             2048U
#define ADS1110_CODE_FULL_SCALE     32768.0f

#define ADS1110_READ_LEN            3U

#endif /* ADS1110_REGISTER_H */
