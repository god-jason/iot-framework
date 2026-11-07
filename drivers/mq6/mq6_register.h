#ifndef MQ6_REGISTER_H
#define MQ6_REGISTER_H

/*
 * MQ-6 LPG（液化石油气）传感器——模拟量器件，无寄存器地址。
 * 仅定义模块级电气默认值；灵敏度曲线需按 datasheet 标定。
 */

#define MQ6_DEFAULT_VC_MV       5000U
#define MQ6_DEFAULT_RL_OHM      10000U
#define MQ6_WARMUP_MS           180000U
#define MQ6_MIN_VALID_MV        50U

#endif /* MQ6_REGISTER_H */
