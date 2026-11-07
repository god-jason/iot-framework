#ifndef PORT_EXTI_H
#define PORT_EXTI_H

/*
 * 外部中断：为指定引脚配置边沿触发、优先级与回调。
 *
 * 回调在中断上下文执行，应尽量短小；同一引脚重复初始化会覆盖原配置。
 * 未初始化或未使能的引脚，其 enable/disable/clear 返回 PORT_NOT_READY。
 */

#include "port_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 触发边沿 */
typedef enum {
    PORT_EXTI_RISING = 0,   /* 上升沿 */
    PORT_EXTI_FALLING,      /* 下降沿 */
    PORT_EXTI_BOTH          /* 双边沿 */
} port_exti_trigger_t;

/*
 * 外部中断回调。
 * pin       : 触发中断的逻辑引脚；
 * user_data : 注册时绑定的上下文，可为 NULL。
 * 注意：执行于中断上下文，禁止阻塞。
 */
typedef void (*port_exti_callback_t)(port_pin_t pin, void *user_data);

/* 外部中断配置 */
typedef struct {
    port_exti_trigger_t trigger;    /* 触发边沿 */
    uint8_t priority;               /* 中断优先级，数值含义由平台定义（通常越小越高） */

    port_exti_callback_t callback;  /* 中断回调，可为 NULL（仅轮询标志时） */
    void *user_data;                /* 回调上下文，可为 NULL */
} port_exti_config_t;

/*
 * 初始化外部中断。
 * pin    : 逻辑引脚编码。
 * config : 配置项，不可为 NULL。
 * 返回   : PORT_OK 成功；PORT_INVALID_PARAM；引脚不支持中断返回 PORT_NOT_SUPPORTED。
 * 注意   : 初始化后默认未必使能，需再调用 port_exti_enable()。
 */
port_status_t port_exti_init(port_pin_t pin, const port_exti_config_t *config);

/* 使能指定引脚的外部中断；返回 PORT_OK / PORT_INVALID_PARAM / PORT_NOT_READY */
port_status_t port_exti_enable(port_pin_t pin);

/* 关闭指定引脚的外部中断；返回 PORT_OK / PORT_INVALID_PARAM / PORT_NOT_READY */
port_status_t port_exti_disable(port_pin_t pin);

/* 清除挂起的中断标志；返回 PORT_OK / PORT_INVALID_PARAM */
port_status_t port_exti_clear(port_pin_t pin);

/* 反初始化并释放该引脚的中断资源；返回 PORT_OK / PORT_INVALID_PARAM */
port_status_t port_exti_deinit(port_pin_t pin);

#ifdef __cplusplus
}
#endif

#endif /* PORT_EXTI_H */
