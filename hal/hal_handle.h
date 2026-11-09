#ifndef HAL_HANDLE_H
#define HAL_HANDLE_H

/*
 * HAL 通用句柄与回调定义：句柄基类、事件回调、超时常量。
 * 各外设句柄（hal_xxx_t）的第一个成员必须是 hal_handle_base_t。
 */

#include <stdint.h>
#include <stdbool.h>

#include "hal_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- 通用句柄 ---------------- */
/*
 * 通用句柄基类。
 * 各外设句柄（hal_xxx_t）的第一个成员必须是本结构，
 * 以便平台层通过强制转换统一处理生命周期与错误上报。
 */
typedef struct {
    void *instance;             /* 平台外设实例（寄存器基址 / HAL 句柄指针 / 平台槽位） */
    hal_state_e state;         /* 当前运行状态，HAL_STATE_RESET 表示未初始化 */
    int error;        /* 最近一次错误码，成功时为 HAL_OK */
} hal_handle_base_t;

/* ---------------- 通用回调 ---------------- */

/*
 * 通用事件回调。
 * handle    : 触发事件的句柄（由实现传入，可能是具体外设句柄指针）；
 * event     : 事件类型；
 * user_data : 注册回调时绑定的用户上下文，可为 NULL。
 * 注意：回调通常在中断上下文执行，禁止阻塞或耗时操作。
 */
typedef void (*hal_callback_t)(void *handle, hal_event_e event, void *user_data);

/* ---------------- 超时 ---------------- */
#define HAL_TIMEOUT_FOREVER    UINT32_MAX  /* 无限等待 */
#define HAL_TIMEOUT_NONE       0U          /* 不等待，立即返回 */

#ifdef __cplusplus
}
#endif

#endif /* HAL_HANDLE_H */
