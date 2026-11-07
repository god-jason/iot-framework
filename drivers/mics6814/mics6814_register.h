#ifndef MICS6814_REGISTER_H
#define MICS6814_REGISTER_H

/*
 * MiCS-6814 三通道气体传感器（RED=CO，OX=NO2，NH3）——模拟量器件，无寄存器。
 *
 * 三个通道各自独立输出分压电压，需要各自标定 R0 与曲线；本文件只定义
 * 模块级电气默认值，灵敏度曲线需按 datasheet 标定。
 */

#define MICS6814_CH_COUNT        3U

/* 加热回路供电（典型 5V 或 3.3V 模组，按实物核对） */
#define MICS6814_DEFAULT_VC_MV   5000U
/* 各通道负载电阻 RL，MiCS-6814 常见 10k~47k，按模组原理图核对 */
#define MICS6814_DEFAULT_RL_OHM  10000U
/* 预热/老化时间 */
#define MICS6814_WARMUP_MS       180000U
/* 低于该电压视为该通道无有效信号 */
#define MICS6814_MIN_VALID_MV    50U

#endif /* MICS6814_REGISTER_H */
