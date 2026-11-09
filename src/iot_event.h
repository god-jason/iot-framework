#ifndef IOT_EVENT_H
#define IOT_EVENT_H

/*
 * iot_event.h - 事件组（iot_event.c）
 *
 * 32 位事件标志，支持「任一/全部满足」等待，以及返回前自动清位。
 * 典型用途：用一组位同时等待「网络就绪 + 配置完成 + 允许上报」。
 */

#include "iot_types.h"
#include "iot_wait.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 等待选项 */
#define IOT_EVENT_ANY    0x00U   /* 任一位满足即返回 */
#define IOT_EVENT_ALL    0x01U   /* 所有请求位满足才返回 */
#define IOT_EVENT_CLEAR  0x02U   /* 返回前清除已满足的位 */

struct iot_event {
    uint32_t flags;                 /* 当前事件标志位 */
    iot_wait_queue_t wait;         /* 等待该事件的任务 */
    bool initialized;               /* 初始化标志 */
};

#if IOT_USE_EVENT

/* 初始化事件组，标志位清零 */
int iot_event_init(iot_event_t *event);

/* 置位，并唤醒所有因此满足等待条件的任务 */
int iot_event_set(iot_event_t *event, uint32_t bits);

/* 清位 */
int iot_event_clear(iot_event_t *event, uint32_t bits);

/* 读取当前标志位 */
int iot_event_get(iot_event_t *event, uint32_t *flags);

/*
 * 等待事件位。
 *   bits    : 关注的位（不能为 0）
 *   options : IOT_EVENT_ANY / IOT_EVENT_ALL / IOT_EVENT_CLEAR 组合
 *   timeout : IOT_TIMEOUT_FOREVER 永久等待；IOT_TIMEOUT_NONE 立即返回
 *   out_flags: 可空，返回满足时刻的完整标志位
 * 返回 IOT_OK / IOT_TIMEOUT / IOT_INVALID_PARAM / IOT_NOT_READY
 */
int iot_event_wait(iot_event_t *event, uint32_t bits, uint8_t options,
                   uint32_t timeout, uint32_t *out_flags);

#endif /* IOT_USE_EVENT */

#ifdef __cplusplus
}
#endif

#endif /* IOT_EVENT_H */
