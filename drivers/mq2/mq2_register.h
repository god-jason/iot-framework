#ifndef MQ2_REGISTER_H
#define MQ2_REGISTER_H

/*
 * MQ-2 可燃气体传感器（LPG / 甲烷 / 氢气 / 烟雾）——模拟量器件，无寄存器地址。
 *
 * 本文件只定义模块级电气默认值；灵敏度曲线（Rs/R0 与 ppm 的关系）与器件、
 * 老化、供电强相关，必须按 datasheet 曲线标定，这里不提供任何编造系数。
 */

/* 加热回路与分压回路供电，常见 5V 模组 */
#define MQ2_DEFAULT_VC_MV       5000U
/* 模块负载电阻 RL，常见 10k */
#define MQ2_DEFAULT_RL_OHM      10000U
/* 上电预热/老化时间：MQ 系列首次使用建议老化 24h 以上 */
#define MQ2_WARMUP_MS           180000U
/* 低于该电压视为探头未工作（断线/未预热），不参与浓度换算 */
#define MQ2_MIN_VALID_MV        50U

#endif /* MQ2_REGISTER_H */
