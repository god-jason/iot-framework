#ifndef MQ3_REGISTER_H
#define MQ3_REGISTER_H

/*
 * MQ-3 酒精传感器——模拟量器件，无寄存器地址。
 * 仅定义模块级电气默认值；灵敏度曲线需按 datasheet 标定。
 */

#define MQ3_DEFAULT_VC_MV       5000U
#define MQ3_DEFAULT_RL_OHM      10000U
#define MQ3_WARMUP_MS           180000U
#define MQ3_MIN_VALID_MV        50U

#endif /* MQ3_REGISTER_H */
