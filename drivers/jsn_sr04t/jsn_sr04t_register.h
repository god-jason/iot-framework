#ifndef JSN_SR04T_REGISTER_H
#define JSN_SR04T_REGISTER_H

/*
 * JSN-SR04T 防水超声波测距模块时序与量程参数。
 *
 * TRIG 拉高 10us 触发一次测量，模块在 ECHO 上输出与往返时间成正比的高电平：
 *   distance_m = echo_us / (58.0 * 100)
 */

/* 触发时序（us） */
#define JSN_SR04T_TRIG_IDLE_US      5U
#define JSN_SR04T_TRIG_PULSE_US     10U

/* 两次测量之间的最小间隔（ms），datasheet 建议 > 60ms */
#define JSN_SR04T_CYCLE_MS          60U

/* ECHO 电平等待/高电平测量超时（us） */
#define JSN_SR04T_TIMEOUT_US        30000U

/* 换算系数：声波往返 1cm 对应 58us */
#define JSN_SR04T_US_PER_CM         58.0f

/* 量程（m）：模块典型 0.25m ~ 4.5m，按较宽的 0.2m ~ 6.0m 判定 */
#define JSN_SR04T_MIN_RANGE_M       0.2f
#define JSN_SR04T_MAX_RANGE_M       6.0f

#endif /* JSN_SR04T_REGISTER_H */
