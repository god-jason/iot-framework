#ifndef VEML3328_REGISTER_H
#define VEML3328_REGISTER_H

/*
 * VEML3328 RGB/IR 颜色传感器寄存器与常量（Vishay，I2C 7 位地址 0x10）。
 *
 * 通道数据从 0x05 开始连续排列，每通道 2 字节且低字节在前：
 *   0x05/0x06 = Clear，0x07/0x08 = Red，0x09/0x0A = Green，
 *   0x0B/0x0C = Blue，0x0D/0x0E = IR
 * 旧驱动从 0x05 起读 8 字节并当成 R/G/B/IR，漏掉了 Clear 通道导致通道错位，
 * 本驱动按 5 通道 10 字节读取。
 */

#define VEML3328_DEFAULT_ADDRESS    0x10U

#define VEML3328_REG_CONF           0x00U
#define VEML3328_REG_ID             0x04U
#define VEML3328_REG_DATA_START     0x05U

/* 器件 ID 期望值（部分兼容片不实现该寄存器，驱动只作调试输出） */
#define VEML3328_ID_VALUE           0x0028U

#define VEML3328_CHANNELS           5U      /* C, R, G, B, IR */
#define VEML3328_DATA_LEN           (VEML3328_CHANNELS * 2U)

/* CONF(0x00) 位域 */
#define VEML3328_CONF_SD            0x0001U /* 1 = 关断 */
#define VEML3328_CONF_TRIG          0x0002U /* 单次触发 */
#define VEML3328_CONF_AF_SHIFT      2U      /* 自动量程 */
#define VEML3328_CONF_AF_MASK       0x000CU
#define VEML3328_CONF_DG_SHIFT      4U      /* 差分增益 */
#define VEML3328_CONF_DG_MASK       0x0030U
#define VEML3328_CONF_GAIN_SHIFT    6U      /* 增益 */
#define VEML3328_CONF_GAIN_MASK     0x00C0U
#define VEML3328_CONF_IT_SHIFT      8U      /* 积分时间 */
#define VEML3328_CONF_IT_MASK       0x0300U

/* 默认配置：使能测量、关闭自动量程、DG=1:1、增益=1:1、IT=100ms */
#define VEML3328_CONF_DEFAULT       0x0100U
#define VEML3328_IT_100MS           100U
#define VEML3328_MEASURE_DELAY_MS   110U    /* IT + 10% 余量 */

/*
 * 照度换算系数（lux / count）。
 *
 * 说明：VEML3328 输出的是各通道 ADC 计数，照度需按 Vishay 应用笔记用
 * 积分时间/增益查表并对 IR 做加权扣除。本驱动采用固定配置
 * （IT=100ms、DG=1:1、增益=1:1），因此折算为一个固定系数：
 * 该系数为标称值（约 8 counts/lux），未经积分球标定，
 * 只保证量级正确与线性趋势，绝对误差可能较大。
 * 需要绝对照度时请用标准照度计对 counts 现场标定后覆盖此系数。
 */
#define VEML3328_LUX_PER_COUNT      0.125f

#endif /* VEML3328_REGISTER_H */
