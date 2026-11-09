#ifndef IOT_TYPES_H
#define IOT_TYPES_H

/*
 * iot_types.h - 内核公共基础定义
 *
 * 只放跨模块共享的内容：
 *   - 公共常量与状态码；
 *   - 所有内核对象的不完全类型（前向声明）；
 *   - 对象有效性与日志 tag 宏。
 *
 * 各对象的完整结构体定义在对应的 iot_xxx.h 中：
 *   iot_task.h    iot_task_t / iot_task_state_e / iot_entry_t
 *   iot_sem.h     iot_sem_t
 *   iot_mutex.h   iot_mutex_t
 *   iot_event.h   iot_event_t / IOT_EVENT_*
 *   iot_queue.h   iot_queue_t
 *   iot_sbuf.h    iot_sbuf_t
 *   iot_msgq.h    iot_msgq_t
 *   iot_timers.h  iot_timer_t / iot_timer_cb_t
 *   iot_wait.h    iot_wait_queue_t
 *   iot_stats.h   iot_stats_t / iot_task_stats_t
 *
 * 只依赖类型声明的模块包含本文件即可；需要访问成员时再包含对应头文件。
 *
 * 设计约束：
 *   - 对象默认由调用者静态或栈上分配；需要运行期增减时使用
 *     iot_*_create()/iot_*_destroy() 动态接口（内部走 iot_mem_*）；
 *   - 优先级数值越小优先级越高，0 为最高优先级。
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "iot_config.h"  /* 裁剪开关：时间片/栈检查/优先级继承/统计等 */
#include "list.h"        /* 侵入式链表：就绪/延时/等待队列复用 */

#ifdef __cplusplus
extern "C" {
#endif

/* ============================== 常量 ============================== */

/* 优先级数量：0..IOT_MAX_PRIORITY-1 */
#define IOT_MAX_PRIORITY       32
/* 最高优先级（最紧急） */
#define IOT_PRIORITY_HIGHEST   0U
/* 最低优先级，保留给内置空闲任务 */
#define IOT_PRIORITY_IDLE      (IOT_MAX_PRIORITY - 1)

/* 任务名建议最大长度（仅约定，内核不强制截断） */
#define IOT_NAME_MAX           16

/* 无限等待 / 不等待 */
#define IOT_TIMEOUT_FOREVER    UINT32_MAX
#define IOT_TIMEOUT_NONE       0U

/* 内核日志 tag，避免各处重复字符串字面量 */
#define IOT_TAG "iot"

/* 对象是否已初始化（所有内核对象都带 initialized 字段） */
#define IOT_OBJ_VALID(p) ((p) != NULL && (p)->initialized)

/* ============================== 状态码 ============================== */

/* 状态值统一以 int 返回，枚举仅用于定义常量名 */
enum {
    IOT_OK = 0,            /* 成功 */
    IOT_ERROR = -1,        /* 未分类错误 */
    IOT_TIMEOUT = -2,      /* 等待超时 */
    IOT_INVALID_PARAM = -3,/* 参数非法（空指针/越界） */
    IOT_NO_MEMORY = -4,    /* 资源不足（任务/等待队列已满等） */
    IOT_NOT_READY = -5,    /* 未初始化或前置条件不满足 */
    IOT_BUSY = -6,         /* 资源被占用 */
    IOT_NOT_OWNER = -7,    /* 当前任务不是该资源的持有者 */
    IOT_OVERFLOW = -8,     /* 计数溢出（信号量超过上限等） */
    IOT_FORBIDDEN_IN_ISR = -9 /* 中断上下文中调用了可能阻塞的 API */
};

/* ============================== 对象前向声明 ============================== */

typedef struct iot_task        iot_task_t;
typedef struct iot_sem         iot_sem_t;
typedef struct iot_mutex       iot_mutex_t;
typedef struct iot_event       iot_event_t;
typedef struct iot_queue       iot_queue_t;
typedef struct iot_sbuf        iot_sbuf_t;
typedef struct iot_msgq        iot_msgq_t;
typedef struct iot_timer       iot_timer_t;
typedef struct iot_wait_queue  iot_wait_queue_t;

#ifdef __cplusplus
}
#endif

#endif /* IOT_TYPES_H */
