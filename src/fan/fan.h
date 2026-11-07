#ifndef FAN_H
#define FAN_H

/*
 * PWM 风扇：占空比控制转速（0..1000 = 0..100%）。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int (*set_duty)(void *ctx, uint16_t duty);   /* 0..1000 */
    void *ctx;
} fan_ops_t;

typedef struct {
    const fan_ops_t *ops;
    uint16_t duty;
} fan_t;

int fan_init(fan_t *fan, const fan_ops_t *ops);
int fan_set_speed(fan_t *fan, uint16_t duty);
int fan_on(fan_t *fan);      /* 全速 */
int fan_off(fan_t *fan);     /* 停止 */

#ifdef __cplusplus
}
#endif

#endif /* FAN_H */