#ifndef IOT_HANDLE_H
#define IOT_HANDLE_H

/*
 * IOT 通用句柄与回调定义：句柄基类、事件回调、超时常量。
 * 各外设句柄（iot_xxx_t）的第一个成员必须是 iot_handle_base_t。
 */

#include <stdint.h>
#include <stdbool.h>

#include "iot_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- 通用句柄 ---------------- */
/*
 * 通用句柄基类。
 * 各外设句柄（iot_xxx_t）的第一个成员必须是本结构，
 * 以便平台层通过强制转换统一处理生命周期与错误上报。
 */
typedef struct {
    void *instance;             /* 平台外设实例（寄存器基址 / HAL 句柄指针 / 平台槽位） */
    iot_state_t state;         /* 当前运行状态，IOT_STATE_RESET 表示未初始化 */
    iot_status_t error;        /* 最近一次错误码，成功时为 IOT_OK */
} iot_handle_base_t;

/* ---------------- 通用回调 ---------------- */

/*
 * 通用事件回调。
 * handle    : 触发事件的句柄（由实现传入，可能是具体外设句柄指针）；
 * event     : 事件类型；
 * user_data : 注册回调时绑定的用户上下文，可为 NULL。
 * 注意：回调通常在中断上下文执行，禁止阻塞或耗时操作。
 */
typedef void (*iot_callback_t)(void *handle, iot_event_t event, void *user_data);

/* ---------------- 超时 ---------------- */
#define IOT_TIMEOUT_FOREVER    UINT32_MAX  /* 无限等待 */
#define IOT_TIMEOUT_NONE       0U          /* 不等待，立即返回 */

#ifdef __cplusplus
}
#endif

#endif /* IOT_HANDLE_H */
