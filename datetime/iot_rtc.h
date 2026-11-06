#ifndef IOT_RTC_H
#define IOT_RTC_H

/*
 * RTC 抽象：可注册硬件 RTC；未注册时回退到软件时钟（iot_datetime）。
 */

#include "iot_types.h"
#include "iot_datetime.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int (*init)(void *ctx);
    int (*get)(void *ctx, iot_datetime_t *dt);
    int (*set)(void *ctx, const iot_datetime_t *dt);
    void *ctx;
} iot_rtc_ops_t;

int iot_rtc_register(const iot_rtc_ops_t *ops);

int iot_rtc_get(iot_datetime_t *dt);
int iot_rtc_set(const iot_datetime_t *dt);

#ifdef __cplusplus
}
#endif

#endif /* IOT_RTC_H */