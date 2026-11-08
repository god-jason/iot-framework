#ifndef FAN_H
#define FAN_H

/*
 * PWM 风扇：占空比控制转速（0..1000 = 0..100%）。
 *
 * 模块说明：
 *   - 档位与占空比一一对应，超过 1000 时自动钳位；
 *   - 硬件输出通过 fan_ops_t 注入，模块本身不直接操作外设。
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
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

#ifdef __cplusplus
}
#endif

#endif /* FAN_H */