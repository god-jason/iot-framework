#ifndef MQ8_REGISTER_H
#define MQ8_REGISTER_H

/*
 * MQ-8 氢气传感器——模拟量器件，无寄存器地址。
 * 仅定义模块级电气默认值；灵敏度曲线需按 datasheet 标定。
 */

#define MQ8_DEFAULT_VC_MV       5000U
#define MQ8_DEFAULT_RL_OHM      10000U
#define MQ8_WARMUP_MS           180000U
#define MQ8_MIN_VALID_MV        50U

#endif /* MQ8_REGISTER_H */
