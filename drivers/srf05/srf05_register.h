#ifndef SRF05_REGISTER_H
#define SRF05_REGISTER_H

/*
 * SRF05 超声波测距模块时序与量程参数（2 线模式）。
 *
 * MODE 引脚悬空时工作在 2 线模式，接口与 HC-SR04 兼容：
 * TRIG 拉高 10us 触发，ECHO 输出与往返时间成正比的高电平。
 *   distance_m = echo_us / (58.0 * 100)
 */

/* 触发时序（us） */
#define SRF05_TRIG_IDLE_US      2U
#define SRF05_TRIG_PULSE_US     10U

/* 两次测量之间的最小间隔（ms），datasheet 建议 > 50ms */
#define SRF05_CYCLE_MS          50U

/* ECHO 电平等待/高电平测量超时（us） */
#define SRF05_TIMEOUT_US        30000U

/* 换算系数：声波往返 1cm 对应 58us */
#define SRF05_US_PER_CM         58.0f

/* 量程（m）：模块标称 1cm ~ 4m，按 4.5m 判定溢出 */
#define SRF05_MIN_RANGE_M       0.03f
#define SRF05_MAX_RANGE_M       4.5f

#endif /* SRF05_REGISTER_H */
