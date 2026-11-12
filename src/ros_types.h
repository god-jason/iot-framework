#ifndef ROS_TYPES_H
#define ROS_TYPES_H

/*
 * ros_types.h - 内核公共基础定义
 *
 * 只放跨模块共享的内容：
 *   - 公共常量与状态码；
 *   - 所有内核对象的不完全类型（前向声明）；
 *   - 对象有效性与日志 tag 宏。
 *
 * 各对象的完整结构体定义在对应的 ros_xxx.h 中：
 *   ros_task.h    ros_task_t / ros_task_state_e / ros_entry_t
 *   ros_sem.h     ros_sem_t
 *   ros_mutex.h   ros_mutex_t
 *   ros_event.h   ros_event_t / ROS_EVENT_*
 *   ros_queue.h   ros_queue_t
 *   ros_sbuf.h    ros_sbuf_t
 *   ros_msgq.h    ros_msgq_t
 *   ros_timers.h  ros_timer_t / ros_timer_cb_t
 *   ros_wait.h    ros_wait_queue_t
 *   ros_stats.h   ros_stats_t / ros_task_stats_t
 *
 * 只依赖类型声明的模块包含本文件即可；需要访问成员时再包含对应头文件。
 *
 * 设计约束：
 *   - 对象默认由调用者静态或栈上分配；需要运行期增减时使用
 *     ros_*_create()/ros_*_destroy() 动态接口（内部走 ros_mem_*）；
 *   - 优先级数值越小优先级越高，0 为最高优先级。
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "ros_config.h"  /* 裁剪开关：时间片/栈检查/优先级继承/统计等 */
#include "list.h"        /* 侵入式链表：就绪/延时/等待队列复用 */

#ifdef __cplusplus
extern "C" {
#endif

/* ============================== 常量 ============================== */

/* 优先级数量：0..ROS_MAX_PRIORITY-1 */
#define ROS_MAX_PRIORITY       8
/* 最高优先级（最紧急） */
#define ROS_PRIORITY_HIGHEST   0U
/* 最低优先级，保留给内置空闲任务 */
#define ROS_PRIORITY_IDLE      (ROS_MAX_PRIORITY - 1)

/* 任务名建议最大长度（仅约定，内核不强制截断） */
#define ROS_NAME_MAX           16

/* 无限等待 / 不等待 */
#define ROS_TIMEOUT_FOREVER    UINT32_MAX
#define ROS_TIMEOUT_NONE       0U

/* 内核日志 tag，避免各处重复字符串字面量 */
#define ROS_TAG "ros"

/* 对象是否已初始化（所有内核对象都带 initialized 字段） */
#define ROS_OBJ_VALID(p) ((p) != NULL && (p)->initialized)

/* ============================== 内部工具 ============================== */

/*
 * 取 32 位整数最低置位编号（0..31），v 为 0 时返回 -1。
 * 优先级 0 最高，因此结果直接就是最高优先级编号。
 * 就绪位图与等待队列位图共用：GCC/Clang 走硬件指令，其他编译器用循环。
 */
static inline int ros_ctz32(uint32_t v)
{
#if defined(__GNUC__) || defined(__clang__)
    return (v == 0U) ? -1 : __builtin_ctz(v);
#else
    int n = 0;

    if (v == 0U) {
        return -1;
    }
    while ((v & 1U) == 0U) {
        v >>= 1;
        n++;
    }
    return n;
#endif
}

/* ============================== 状态码 ============================== */

/* 状态值统一以 int 返回，枚举仅用于定义常量名 */
enum {
    ROS_OK = 0,            /* 成功 */
    ROS_ERROR = -1,        /* 未分类错误 */
    ROS_TIMEOUT = -2,      /* 等待超时 */
    ROS_INVALID_PARAM = -3,/* 参数非法（空指针/越界） */
    ROS_NO_MEMORY = -4,    /* 资源不足（任务/等待队列已满等） */
    ROS_NOT_READY = -5,    /* 未初始化或前置条件不满足 */
    ROS_BUSY = -6,         /* 资源被占用 */
    ROS_NOT_OWNER = -7,    /* 当前任务不是该资源的持有者 */
    ROS_OVERFLOW = -8,     /* 计数溢出（信号量超过上限等） */
    ROS_FORBIDDEN_IN_ISR = -9 /* 中断上下文中调用了可能阻塞的 API */
};

/* ============================== 对象前向声明 ============================== */

typedef struct ros_task        ros_task_t;
typedef struct ros_sem         ros_sem_t;
typedef struct ros_mutex       ros_mutex_t;
typedef struct ros_event       ros_event_t;
typedef struct ros_queue       ros_queue_t;
typedef struct ros_sbuf        ros_sbuf_t;
typedef struct ros_msgq        ros_msgq_t;
typedef struct ros_timer       ros_timer_t;
typedef struct ros_wait_queue  ros_wait_queue_t;

#ifdef __cplusplus
}
#endif

#endif /* ROS_TYPES_H */
