#ifndef PORT_OS_H
#define PORT_OS_H

/*
 * OS 适配接口：时间、锁、信号量、临界区。
 * 公共头文件不包含任何 RTOS 头；NoOS 平台可返回 PORT_NOT_SUPPORTED。
 */

#include "port_def.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uintptr_t port_mutex_t;
typedef uintptr_t port_semaphore_t;
typedef uintptr_t port_thread_t;

typedef struct {
    port_status_t (*mutex_create)(port_mutex_t *mutex);
    port_status_t (*mutex_delete)(port_mutex_t mutex);

    port_status_t (*mutex_lock)(port_mutex_t mutex, uint32_t timeout);
    port_status_t (*mutex_unlock)(port_mutex_t mutex);

    port_status_t (*semaphore_create)(port_semaphore_t *sem,
                                      uint32_t max_count,
                                      uint32_t initial_count);
    port_status_t (*semaphore_wait)(port_semaphore_t sem, uint32_t timeout);
    port_status_t (*semaphore_post)(port_semaphore_t sem);

    uint32_t (*tick_get)(void);

    void (*delay_ms)(uint32_t ms);

    void (*critical_enter)(void);
    void (*critical_exit)(void);
} port_os_ops_t;

port_status_t port_os_register(const port_os_ops_t *ops);
const port_os_ops_t *port_os_get(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_OS_H */
