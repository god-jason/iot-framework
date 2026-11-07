#ifndef MQ7_REGISTER_H
#define MQ7_REGISTER_H

/*
 * MQ-7 一氧化碳传感器——模拟量器件，无寄存器地址。
 *
 * 注意：MQ-7 需要 5V/1.4V 高低压循环加热（约 60s 高压 + 90s 低压）才能
 * 获得稳定读数，加热时序由上层定时器实现，本驱动只做电压 -> 浓度换算。
 * 灵敏度曲线需按 datasheet 标定。
 */

#define MQ7_DEFAULT_VC_MV       5000U
#define MQ7_DEFAULT_RL_OHM      10000U
#define MQ7_WARMUP_MS           180000U
#define MQ7_MIN_VALID_MV        50U

#endif /* MQ7_REGISTER_H */
