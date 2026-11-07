#ifndef MCP3421_REGISTER_H
#define MCP3421_REGISTER_H

/*
 * MCP3421 18 位单通道 Δ-Σ ADC（I2C，固定地址 0x68，内置 2.048V 基准）。
 *
 * 配置字节：
 *   bit7 RDY(只读) | bit6:5 C(转换模式) | bit4:2 S(分辨率/增益) | bit1:0 G(增益)
 *   C: 00=单次 01=连续 10/11=保留
 *   S: 000=12bit 001=14bit 010=16bit 011=18bit
 *      100=12bit×2 101=14bit×2 110=16bit×2 111=18bit×2
 *   G: 00=×1 01=×2 10=×4 11=×8
 *
 * 18 位模式下读 3 字节（低 6 位为无效填充位），其余模式读 2 字节。
 */

#define MCP3421_DEFAULT_ADDRESS     0x68U

/* 配置字节位 */
#define MCP3421_CFG_RDY             0x80U   /* 只读：0 表示新数据就绪 */
#define MCP3421_CFG_MODE_SHIFT      5U
#define MCP3421_CFG_MODE_MASK       0x60U
#define MCP3421_CFG_S_SHIFT         2U
#define MCP3421_CFG_S_MASK          0x1CU
#define MCP3421_CFG_G_SHIFT         0U
#define MCP3421_CFG_G_MASK          0x03U
#define MCP3421_CFG_FIELDS_MASK     0x7FU   /* 除 RDY 外的可写位 */

/* 单次转换时间（ms），datasheet：240/60/15/3.75 SPS */
#define MCP3421_CONV_MS_12BIT       5U
#define MCP3421_CONV_MS_14BIT       18U
#define MCP3421_CONV_MS_16BIT       68U
#define MCP3421_CONV_MS_18BIT       268U
#define MCP3421_CONV_EXTRA_MS       2U

/* 内部基准 2.048V，满量程为 ±2.048V */
#define MCP3421_VREF_MV             2048U
#define MCP3421_FS_MV               4096U

#define MCP3421_FRAME_MAX_LEN       3U

#endif /* MCP3421_REGISTER_H */
