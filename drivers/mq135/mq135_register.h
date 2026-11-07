#ifndef MQ135_REGISTER_H
#define MQ135_REGISTER_H

/*
 * MQ-135 空气质量（NH3 / 苯系物 / 烟雾 / CO2 等）传感器——模拟量器件，
 * 无寄存器地址。多气体共用一个探头，不同气体的曲线不同，必须按目标
 * 气体在 datasheet 中选择曲线标定；此处不提供任何曲线系数。
 */

#define MQ135_DEFAULT_VC_MV       5000U
#define MQ135_DEFAULT_RL_OHM      10000U
#define MQ135_WARMUP_MS           180000U
#define MQ135_MIN_VALID_MV        50U

#endif /* MQ135_REGISTER_H */
