#ifndef MQ4_REGISTER_H
#define MQ4_REGISTER_H

/*
 * MQ-4 甲烷（天然气）传感器——模拟量器件，无寄存器地址。
 * 仅定义模块级电气默认值；灵敏度曲线需按 datasheet 标定。
 */

#define MQ4_DEFAULT_VC_MV       5000U
#define MQ4_DEFAULT_RL_OHM      10000U
#define MQ4_WARMUP_MS           180000U
#define MQ4_MIN_VALID_MV        50U

#endif /* MQ4_REGISTER_H */
