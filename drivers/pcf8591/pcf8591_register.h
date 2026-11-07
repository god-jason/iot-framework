#ifndef PCF8591_REGISTER_H
#define PCF8591_REGISTER_H

/*
 * PCF8591 8 位 4 通道 ADC + 1 路 DAC（I2C，地址 0x48~0x4F）。
 *
 * 控制字节：
 *   bit7 固定 0 | bit6 模拟输出使能 | bit5:4 输入模式 | bit3 自动增量
 *   bit2:1 通道号 | bit0 固定 0
 *
 * 注意：ADC 为流水线结构，选择通道后的第一次读取返回的是上一次转换结果，
 * 因此驱动内部连续读两次，取第二次的值。
 */

#define PCF8591_DEFAULT_ADDRESS     0x48U
#define PCF8591_ADDRESS_MAX         0x4FU

/* 控制字节位 */
#define PCF8591_CTRL_ANALOG_OUT     0x40U   /* bit6: 1 使能 AOUT 输出 */
#define PCF8591_CTRL_MODE_SHIFT     4U
#define PCF8591_CTRL_MODE_MASK      0x30U
#define PCF8591_CTRL_AUTO_INC       0x04U   /* bit3: 自动增量通道 */
#define PCF8591_CTRL_CH_SHIFT       1U
#define PCF8591_CTRL_CH_MASK        0x06U

/* 通道与分辨率 */
#define PCF8591_CHANNEL_COUNT       4U
#define PCF8591_RESOLUTION          256U
#define PCF8591_FULL_SCALE          255U

/* 默认参考电压（模块通常与 VDD 相同） */
#define PCF8591_DEFAULT_VREF_MV     5000U

/* 选择通道后等待采样（ms） */
#define PCF8591_CONV_DELAY_MS       1U

#endif /* PCF8591_REGISTER_H */
