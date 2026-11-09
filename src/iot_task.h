#ifndef IOT_TASK_H
#define IOT_TASK_H

/*
 * iot_task.h - 任务管理（iot_task.c）
 *
 * 静态分配：任务控制块与栈由调用者提供，内核不申请内存。
 * 任务状态、入口回调类型与 iot_task_t 完整定义都在本文件。
 */

#include "iot_types.h"
#include "iot_wait.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================== 任务状态 ============================== */

typedef enum {
    IOT_TASK_READY = 0,    /* 就绪，等待调度 */
    IOT_TASK_RUNNING,      /* 正在运行 */
    IOT_TASK_BLOCKED,      /* 阻塞在信号量/互斥量/队列上 */
    IOT_TASK_DELAYED,      /* 睡眠中，等待 tick 到期 */
    IOT_TASK_TERMINATED,   /* 已退出，不再参与调度 */
    IOT_TASK_SUSPENDED     /* 被挂起，不参与调度（可恢复） */
} iot_task_state_e;

/* ============================== 回调类型 ============================== */

typedef void (*iot_entry_t)(void *arg);        /* 任务入口 */

/* ============================== 任务控制块 ============================== */

struct iot_task {
    /* 移植层上下文：PC 端为 Fiber 句柄，STM32 端为保存的栈指针。
     * 必须是第一个成员：Cortex-M PendSV 汇编用 [tcb] 直接存取栈指针。 */
    void *stack_ptr;
    void *stack_mem;                /* 任务栈内存（调用者提供） */
    uint32_t stack_size;            /* 任务栈大小（字节） */

    const char *name;               /* 任务名（只读引用，不拷贝） */
    iot_entry_t entry;             /* 入口函数 */
    void *arg;                      /* 入口参数 */

    uint8_t priority;               /* 当前优先级：0 最高（含继承提升） */
    uint8_t base_priority;          /* 创建时的原始优先级，继承解除后恢复 */
    uint8_t suspend_count;          /* 嵌套挂起计数，减到 0 才恢复就绪 */
    iot_task_state_e state;        /* 当前状态 */

    list_node_t ready_node;         /* 就绪链表节点（每优先级一条环形链） */
    list_node_t delayed_node;       /* 延时链表节点（排序键 wake_tick） */
    list_node_t wait_node;          /* 阻塞等待对象链表节点（sort_key = 挂入时优先级） */
    list_node_t all_node;           /* 任务注册表节点（统计遍历用） */

    iot_wait_queue_t *wait_queue;   /* 当前所在的等待队列，NULL 表示未阻塞 */

    uint32_t wake_tick;             /* 延时/超时唤醒时刻 */
    void *wait_ptr;                 /* 队列等待者的数据缓冲指针 */

    int wait_result;                /* 被唤醒时写入的结果码 */

#if IOT_USE_TIME_SLICING
    uint32_t slice_remaining;       /* 本时间片剩余 tick */
#endif

#if IOT_USE_PRIORITY_INHERIT
    struct iot_mutex *wait_mutex;   /* 阻塞在哪个互斥量上（继承链传播用） */
#endif

#if IOT_USE_EVENT
    uint32_t wait_bits;             /* 事件组等待的位掩码 */
    uint8_t wait_opt;               /* 事件组等待选项 */
#endif

#if IOT_USE_STATS
    uint32_t run_ticks;             /* 累计运行 tick 数 */
    uint32_t switches;              /* 被调度上 CPU 的次数 */
#endif

#if IOT_USE_NOTIFY
    uint32_t notify_value;          /* 任务通知携带的值 */
    bool notify_pending;            /* 是否有未消费的通知 */
#endif

    bool created;                   /* 是否已创建（调试用） */
    bool stack_owned;               /* 栈是否由 iot_task_create_dynamic 分配 */
};

/*
 * 移植层契约：Cortex-M 的 PendSV / iot_port_start_first 汇编用
 * `ldr r0, [tcb]` 取 stack_ptr，因此它必须位于偏移 0。
 * 布局一旦变化，这里会直接编译失败，避免上板后难以定位的栈错乱。
 */
typedef char iot_task_stack_ptr_must_be_first[
    (offsetof(iot_task_t, stack_ptr) == 0U) ? 1 : -1];

/* ============================== API ============================== */

/*
 * 创建任务并加入就绪表。
 *   priority : 0..IOT_PRIORITY_IDLE-1（最低一级保留给空闲任务）
 *   stack_mem/stack_size : 调用者提供的栈内存与字节数
 * 返回 IOT_NO_MEMORY 表示移植层上下文构造失败。
 */
int iot_task_create(iot_task_t *task, const char *name,
                    uint8_t priority, iot_entry_t entry, void *arg,
                    void *stack_mem, uint32_t stack_size);

/*
 * 动态创建任务：TCB 与任务栈均由 iot_mem_* 分配。
 * 返回任务句柄；参数非法或内存不足返回 NULL。
 * 说明：创建后任务处于就绪态；销毁必须调用 iot_task_destroy()，
 *       且不能在任务自己的上下文里销毁自己（应先 iot_task_exit()）。
 */
iot_task_t *iot_task_create_dynamic(const char *name, uint8_t priority,
                                    iot_entry_t entry, void *arg,
                                    uint32_t stack_size);

/*
 * 销毁由 iot_task_create_dynamic() 创建的任务，释放栈、端口上下文与 TCB。
 * 仅允许销毁「就绪但未运行」或「已退出（TERMINATED）」的任务：
 *   - 当前正在运行的任务返回 IOT_BUSY（先 iot_task_exit()，由别的任务销毁）；
 *   - 阻塞/延时中的任务返回 IOT_BUSY（需先唤醒或让它退出）；
 *   - 静态创建的任务返回 IOT_INVALID_PARAM。
 */
int iot_task_destroy(iot_task_t *task);

/* 让出 CPU：当前任务回到同优先级队尾，触发一次调度 */
void iot_yield(void);

/* 终止当前任务并切走（通常不返回） */
void iot_task_exit(void);

/* 睡眠指定毫秒（基于 tick） */
void iot_sleep_ms(uint32_t ms);

/* 睡眠到指定 tick 时刻 */
void iot_sleep_until(uint32_t wake_tick);

/* 调整任务基准优先级（同时解除当前继承提升） */
int iot_task_set_priority(iot_task_t *task, uint8_t priority);

/*
 * 挂起任务：支持嵌套（计数），需同样次数的 resume 才恢复。
 * 仅允许挂起 READY / RUNNING / SUSPENDED 状态的任务；
 * BLOCKED / DELAYED / TERMINATED 返回 IOT_BUSY（与 destroy 一致的保守语义）。
 * 挂起当前任务时函数内部切走，被恢复后才返回。
 */
int iot_task_suspend(iot_task_t *task);

/* 恢复任务：嵌套计数减到 0 时重新进入就绪表 */
int iot_task_resume(iot_task_t *task);

/* 任务当前是否处于挂起态 */
bool iot_task_is_suspended(const iot_task_t *task);

/* 初始化任务注册表与通知队列（iot_init 调用） */
void iot_task_module_init(void);

/* 把任务 TCB 挂入任务注册表（iot_idle_init 使用） */
void iot_task_register(iot_task_t *task);

/* 遍历任务注册表，清零每个任务的运行统计（iot_stats_reset 调用） */
void iot_task_reset_stats_all(void);

#if IOT_USE_NOTIFY

/* 初始化通知等待队列（由 iot_task_module_init 调用） */
void iot_notify_init(void);

/*
 * 向指定任务发送通知：若该任务正等待通知则立即唤醒。
 * 通知值保存到任务控制块，等待方取走后清除 pending。
 */
int iot_task_notify(iot_task_t *task, uint32_t value);

/*
 * 等待本任务的通知。
 *   timeout : IOT_TIMEOUT_FOREVER / IOT_TIMEOUT_NONE / 毫秒数
 *   value   : 可空，返回通知携带的值
 */
int iot_task_wait_notify(iot_task_t *task, uint32_t timeout, uint32_t *value);

/* 清除未消费的通知 */
void iot_task_notify_clear(iot_task_t *task);

#endif /* IOT_USE_NOTIFY */

#ifdef __cplusplus
}
#endif

#endif /* IOT_TASK_H */
