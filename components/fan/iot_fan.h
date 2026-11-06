#ifndef IOT_FAN_H
#define IOT_FAN_H

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
} iot_fan_ops_t;

typedef struct {
    const iot_fan_ops_t *ops;
    uint16_t duty;
} iot_fan_t;

int iot_fan_init(iot_fan_t *fan, const iot_fan_ops_t *ops);
int iot_fan_set_speed(iot_fan_t *fan, uint16_t duty);
int iot_fan_on(iot_fan_t *fan);      /* 全速 */
int iot_fan_off(iot_fan_t *fan);     /* 停止 */

#ifdef __cplusplus
}
#endif

#endif /* IOT_FAN_H */