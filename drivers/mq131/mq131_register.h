#ifndef MQ131_REGISTER_H
#define MQ131_REGISTER_H

/*
 * MQ-131 臭氧（O3）传感器——模拟量器件，无寄存器地址。
 * 有低浓度与高浓度两种版本，灵敏度曲线差异很大，必须按所用版本
 * datasheet 标定；此处不提供任何曲线系数。
 */

#define MQ131_DEFAULT_VC_MV       5000U
#define MQ131_DEFAULT_RL_OHM      10000U   /* 部分模组为 1k，按实物核对 */
#define MQ131_WARMUP_MS           180000U  /* 臭氧传感器预热时间较长 */
#define MQ131_MIN_VALID_MV        50U

#endif /* MQ131_REGISTER_H */
