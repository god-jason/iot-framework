#ifndef VIBRATION_REGISTER_H
#define VIBRATION_REGISTER_H

/*
 * 振动开关（滚珠/弹簧式，如 SW-420）电气参数。
 *
 * 常见模块带比较器数字输出：静止时低电平，振动时输出高电平脉冲。
 * 振动脉冲可能很短，因此读取时做多次采样，任一采样有效即判定振动。
 */

/* 有效电平：1 表示振动时 OUT 为高电平 */
#define VIBRATION_ACTIVE_LEVEL  1U

/* 采样次数与间隔（us）：用于捕捉短脉冲 */
#define VIBRATION_SAMPLES       4U
#define VIBRATION_SAMPLE_US     200U

#endif /* VIBRATION_REGISTER_H */
