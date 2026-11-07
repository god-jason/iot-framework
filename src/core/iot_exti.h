#ifndef IOT_EXTI_H
#define IOT_EXTI_H

/*
 * 外部中断：为指定引脚配置边沿触发、优先级与回调。
 *
 * 回调在中断上下文执行，应尽量短小；同一引脚重复初始化会覆盖原配置。
 * 未初始化或未使能的引脚，其 enable/disable/clear 返回 IOT_NOT_READY。
 */

#include "iot_def.h"
#include "iot_pin.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 触发边沿 */
typedef enum {
    IOT_EXTI_RISING = 0,   /* 上升沿 */
    IOT_EXTI_FALLING,      /* 下降沿 */
    IOT_EXTI_BOTH          /* 双边沿 */
} iot_exti_trigger_t;

/*
 * 外部中断回调。
 * pin       : 触发中断的逻辑引脚；
 * user_data : 注册时绑定的上下文，可为 NULL。
 * 注意：执行于中断上下文，禁止阻塞。
 */
typedef void (*iot_exti_callback_t)(iot_pin_t pin, void *user_data);

/* 外部中断配置 */
typedef struct {
    iot_exti_trigger_t trigger;    /* 触发边沿 */
    uint8_t priority;               /* 中断优先级，数值含义由平台定义（通常越小越高） */

    iot_exti_callback_t callback;  /* 中断回调，可为 NULL（仅轮询标志时） */
    void *user_data;                /* 回调上下文，可为 NULL */
} iot_exti_config_t;

/*
 * 初始化外部中断。
 * pin    : 逻辑引脚编码。
 * config : 配置项，不可为 NULL。
 * 返回   : IOT_OK 成功；IOT_INVALID_PARAM；引脚不支持中断返回 IOT_NOT_SUPPORTED。
 * 注意   : 初始化后默认未必使能，需再调用 iot_exti_enable()。
 */
iot_status_t iot_exti_init(iot_pin_t pin, const iot_exti_config_t *config);

/* 使能指定引脚的外部中断；返回 IOT_OK / IOT_INVALID_PARAM / IOT_NOT_READY */
iot_status_t iot_exti_enable(iot_pin_t pin);

/* 关闭指定引脚的外部中断；返回 IOT_OK / IOT_INVALID_PARAM / IOT_NOT_READY */
iot_status_t iot_exti_disable(iot_pin_t pin);

/* 清除挂起的中断标志；返回 IOT_OK / IOT_INVALID_PARAM */
iot_status_t iot_exti_clear(iot_pin_t pin);

/* 反初始化并释放该引脚的中断资源；返回 IOT_OK / IOT_INVALID_PARAM */
iot_status_t iot_exti_deinit(iot_pin_t pin);

#ifdef __cplusplus
}
#endif

#endif /* IOT_EXTI_H */
