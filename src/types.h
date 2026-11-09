#ifndef IOT_TYPES_H
#define IOT_TYPES_H

/*
 * RTOS 类型定义。
 *
 * 本文件只放常量、状态码、任务状态与各类对象结构体，
 * 不含任何函数声明，便于上层只依赖类型时单独包含。
 *
 * 设计约束：
 *   - 所有对象（任务/信号量/互斥量/队列/定时器）结构体完全公开，
 *     由调用者静态或栈上分配，内核不做任何动态内存申请；
 *   - 优先级数值越小优先级越高，0 为最高优先级。
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "list.h"   /* 侵入式链表：就绪/延时/等待队列复用 */

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
};

/* ============================== 任务状态 ============================== */

typedef enum {
    IOT_TASK_READY = 0,    /* 就绪，等待调度 */
    IOT_TASK_RUNNING,      /* 正在运行 */
    IOT_TASK_BLOCKED,      /* 阻塞在信号量/互斥量/队列上 */
    IOT_TASK_DELAYED,      /* 睡眠中，等待 tick 到期 */
    IOT_TASK_TERMINATED    /* 已退出，不再参与调度 */
} iot_task_state_e;

/* ============================== 回调类型 ============================== */

typedef void (*iot_entry_t)(void *arg);        /* 任务入口 */
typedef void (*iot_timer_cb_t)(void *arg);     /* 定时器回调 */

/* ============================== 任务控制块 ============================== */

typedef struct iot_task {
    /* 移植层上下文：PC 端为 Fiber 句柄，STM32 端为保存的栈指针 */
    void *stack_ptr;
    void *stack_mem;                /* 任务栈内存（调用者提供） */
    uint32_t stack_size;            /* 任务栈大小（字节） */

    const char *name;               /* 任务名（只读引用，不拷贝） */
    iot_entry_t entry;             /* 入口函数 */
    void *arg;                      /* 入口参数 */

    uint8_t priority;               /* 优先级：0 最高 */
    iot_task_state_e state;        /* 当前状态 */

    list_node_t ready_node;    /* 就绪链表节点（每优先级一条环形链） */
    list_node_t delayed_node;  /* 延时链表节点（排序键 wake_tick） */
    list_node_t wait_node;     /* 阻塞等待对象链表节点 */

    uint32_t wake_tick;             /* 延时/超时唤醒时刻 */
    void *wait_ptr;                 /* 队列等待者的数据缓冲指针 */

    int wait_result;      /* 被唤醒时写入的结果码 */

    bool created;                   /* 是否已创建（调试用） */
} iot_task_t;

/* ============================== 计数信号量 ============================== */

typedef struct iot_sem {
    uint32_t count;                 /* 当前计数值 */
    list_t wait;               /* 等待该信号量的任务（wait_node） */
    bool initialized;               /* 初始化标志 */
} iot_sem_t;

/* ============================== 互斥量 ============================== */

/*
 * 轻量互斥量：仅保证互斥与所有权，不实现优先级继承。
 * 需要优先级继承的场景建议改用带继承的互斥量实现。
 */
typedef struct iot_mutex {
    iot_task_t *owner;             /* 当前持有者，NULL 表示空闲 */
    list_t wait;               /* 等待该互斥量的任务（wait_node） */
    bool initialized;               /* 初始化标志 */
} iot_mutex_t;

/* ============================== 定长消息队列 ============================== */

typedef struct iot_queue {
    uint8_t *buffer;                /* 消息缓冲区（调用者提供） */
    uint32_t item_size;             /* 单条消息大小（字节） */
    uint32_t depth;                 /* 队列深度（条数） */
    uint32_t head;                  /* 下一个写入位置 */
    uint32_t tail;                  /* 下一个读取位置 */
    uint32_t count;                 /* 当前消息数 */

    list_t send_wait;          /* 队列满时等待发送的任务 */
    list_t recv_wait;          /* 队列空时等待接收的任务 */

    bool initialized;               /* 初始化标志 */
} iot_queue_t;

/* ============================== 软件定时器 ============================== */

typedef struct iot_timer {
    list_node_t node;          /* 定时器链表节点（排序键 deadline） */
    const char *name;               /* 定时器名 */
    iot_timer_cb_t cb;             /* 到期回调 */
    void *arg;                      /* 回调参数 */
    uint32_t period;                /* 周期（tick） */
    bool periodic;                  /* true=周期，false=单次 */
    bool running;                   /* 是否已启动 */
    uint32_t deadline;              /* 下次到期时刻 */
} iot_timer_t;

#ifdef __cplusplus
}
#endif

#endif /* IOT_TYPES_H */