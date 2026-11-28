#ifndef FAN_H
#define FAN_H

/*
 * PWM 风扇：占空比控制转速（0..1000 = 0..100%），支持 TACH 反馈脉冲测速。
 *
 * 模块说明：
 *   - 档位与占空比一一对应，超过 1000 时自动钳位；
 *   - 硬件输出通过 fan_ops_t 注入，模块本身不直接操作外设；
 *   - TACH 反馈脉冲支持两种接入方式：
 *       1) 中断：测速脚下降沿中断里调用 fan_tach_pulse()；
 *       2) 轮询：周期调用 fan_tach_sample(level, now_ms)，自动识别下降沿；
 *   - 每个测量窗口（FAN_TACH_WINDOW_MS）结束后更新一次 RPM。
 *
 * 典型用法：
 *     fan_init(&fan, &ops);
 *     fan_set_speed(&fan, 600);                 // 60%
 *     // 1ms 周期任务里：
 *     rpm = fan_tach_sample(&fan, hal_gpio_read_level(TACH_PIN), now_ms);
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 测速窗口（ms）：越小响应越快，低速时分辨率越差 */
#ifndef FAN_TACH_WINDOW_MS
#define FAN_TACH_WINDOW_MS       1000U
#endif

/* 默认每转脉冲数（常见风扇为 2） */
#ifndef FAN_TACH_PPR_DEFAULT
#define FAN_TACH_PPR_DEFAULT     2U
#endif

/* 底层 PWM 操作表 */
typedef struct {
    int (*set_duty)(void *ctx, uint16_t duty);   /* 设置占空比，0..1000 */
    void *ctx;                                   /* 驱动私有上下文 */
} fan_ops_t;

/* 风扇对象 */
typedef struct {
    const fan_ops_t *ops;   /* 底层操作表 */
    uint16_t duty;          /* 当前档位（0..1000） */

    uint16_t pulses_per_rev;    /* 每转 TACH 脉冲数 */
    uint32_t pulse_count;       /* 上电累计脉冲数 */
    uint32_t window_pulses;     /* 当前测量窗口内脉冲数 */
    uint32_t window_start_ms;   /* 测量窗口起点（ms） */
    uint32_t rpm;               /* 最近一次计算的转速 */
    bool     tach_started;      /* 是否已建立时间基准 */
    uint8_t  tach_last_level;   /* 上次采样电平（0/1） */
} fan_t;

/*
 * 初始化风扇并以 0 档输出。
 * 参数：fan 对象；ops 操作表（set_duty 必须非 NULL）。
 * 返回：fan_off() 的返回值；参数非法返回 -1。
 */
int fan_init(fan_t *fan, const fan_ops_t *ops);

/*
 * 设置档位（超过 1000 自动钳位）。
 * 参数：fan 对象；duty 0..1000。
 * 返回：底层 set_duty 返回值；fan 为 NULL 返回 -1。
 */
int fan_set_speed(fan_t *fan, uint16_t duty);

/* 全速开启：等价于 fan_set_speed(fan, 1000) */
int fan_on(fan_t *fan);

/* 停止：等价于 fan_set_speed(fan, 0) */
int fan_off(fan_t *fan);

/*
 * 设置每转脉冲数（默认 FAN_TACH_PPR_DEFAULT）。
 * 参数：fan 对象；ppr > 0。
 * 返回：0 成功；参数非法返回 -1。
 */
int fan_set_pulses_per_rev(fan_t *fan, uint16_t ppr);

/*
 * 记录一个 TACH 脉冲（在下降沿中断中调用）。
 * 仅做计数，可在中断上下文安全调用。
 */
void fan_tach_pulse(fan_t *fan);

/*
 * 周期采样 TACH 电平，自动识别下降沿并计数，再按窗口刷新 RPM。
 * 参数：fan 对象；level 当前电平（0/1）；now_ms 当前毫秒时基（需单调，
 *       允许回绕）。
 * 返回：当前 RPM（窗口未结束时返回上一次结果）。
 * 建议调用周期 1ms ~ 10ms。
 */
uint32_t fan_tach_sample(fan_t *fan, uint8_t level, uint32_t now_ms);

/*
 * 仅刷新转速（用于中断计数方案）。
 * 参数：fan 对象；now_ms 当前毫秒时基。
 * 返回：当前 RPM；窗口未结束时返回上一次结果。
 */
uint32_t fan_update_rpm(fan_t *fan, uint32_t now_ms);

/* 读取最近一次转速（RPM）；fan 为 NULL 返回 0 */
uint32_t fan_get_rpm(const fan_t *fan);

/* 读取上电累计 TACH 脉冲数；fan 为 NULL 返回 0 */
uint32_t fan_get_pulse_count(const fan_t *fan);

#ifdef __cplusplus
}
#endif

#endif /* FAN_H */