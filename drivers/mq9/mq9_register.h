#ifndef MQ9_REGISTER_H
#define MQ9_REGISTER_H

/*
 * MQ-9 一氧化碳 / 可燃气体传感器——模拟量器件，无寄存器地址。
 * MQ-9 采用高低压循环加热，加热时序由上层实现。
 * 仅定义模块级电气默认值；灵敏度曲线需按 datasheet 标定。
 */

#define MQ9_DEFAULT_VC_MV       5000U
#define MQ9_DEFAULT_RL_OHM      10000U
#define MQ9_WARMUP_MS           180000U
#define MQ9_MIN_VALID_MV        50U

#endif /* MQ9_REGISTER_H */
