#ifndef HC_SR04_REGISTER_H
#define HC_SR04_REGISTER_H

/* HC-SR04 超声波测距模块时序与换算常数 */

/* 触发脉冲：拉高 10us 以上 */
#define HC_SR04_TRIG_PULSE_US        10U
#define HC_SR04_TRIG_IDLE_US         2U

/* ECHO 等待与量程超时（us）
 * 4m 对应约 23320us 高电平，这里留足余量 */
#define HC_SR04_ECHO_TIMEOUT_US      30000U
#define HC_SR04_TIMEOUT_DEFAULT_US   30000U

/* 声速换算：distance_cm = echo_us / 58.3（往返双程，20℃ 空气） */
#define HC_SR04_US_PER_CM            58.3f
#define HC_SR04_MAX_RANGE_CM         400.0f
#define HC_SR04_MIN_RANGE_CM         2.0f

/* 多次采样平均的最大次数 */
#define HC_SR04_AVG_MAX_SAMPLES      16U

#endif /* HC_SR04_REGISTER_H */
