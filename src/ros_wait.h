#ifndef ROS_WAIT_H
#define ROS_WAIT_H

/*
 * ros_wait.h - 阻塞与唤醒原语（ros_wait.c）
 *
 * 等待队列 ros_wait_queue_t 采用「位图 + 每优先级一条链表」：
 *   - 挂入：按挂入瞬间的优先级入链并置位 → O(1)；
 *   - 取最高优先级等待者：ctz(bitmap) → O(1)；
 *   - 摘除：用 wait_node.sort_key 记住挂入时的优先级，
 *     即使之后被优先级继承提升也能准确回到原链 → O(1)。
 *
 * 所有带阻塞能力的对象（信号量/互斥量/队列/事件组/通知）都内嵌本结构。
 */

#include "ros_types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ros_wait_queue {
    uint32_t bitmap;                       /* bit i = 优先级 i 有等待者 */
    list_t   prio[ROS_MAX_PRIORITY];        /* 每优先级一条 FIFO 链表 */
};

/* 初始化等待队列（对象 init 时调用） */
void ros_wait_queue_init(ros_wait_queue_t *queue);

/* 挂入当前任务（wait_node）；调用者需已持有临界区 */
void ros_wait_queue_add(ros_wait_queue_t *queue, ros_task_t *task);

/* 摘除任务并维护位图；对未挂入的任务安全 */
void ros_wait_queue_remove(ros_wait_queue_t *queue, ros_task_t *task);

/* 取出优先级最高的等待者并摘链；空队列返回 NULL */
ros_task_t *ros_wait_queue_pop_highest(ros_wait_queue_t *queue);

/* 队列中最高优先级；空队列返回 0xFF */
uint8_t ros_wait_queue_top_priority(const ros_wait_queue_t *queue);

/* 任务是否正挂在指定等待队列上 */
bool ros_wait_queue_contains(const ros_wait_queue_t *queue,
                             const ros_task_t *task);

/* 等待队列是否为空（无任何等待者） */
bool ros_wait_queue_empty(const ros_wait_queue_t *queue);

/* 按位图从小到大遍历各非空优先级链（事件组广播唤醒用） */
uint8_t ros_wait_queue_first_prio(uint32_t bitmap);

/* 唤醒队列中的全部等待者（sbuf/msgq 的广播语义） */
void ros_wait_queue_wake_all(ros_wait_queue_t *queue);

/* ------------------------------ 阻塞 / 唤醒 ------------------------------ */

/*
 * 阻塞可行性检查：中断上下文只允许非阻塞调用（timeout == ROS_TIMEOUT_NONE）。
 * 返回 false 时调用方应直接返回 ROS_FORBIDDEN_IN_ISR。
 */
bool ros_wait_blockable(uint32_t timeout);

/*
 * 将当前任务挂到 wait_queue 并阻塞，然后触发调度。
 *   timeout : ROS_TIMEOUT_FOREVER 永久等待；>0 时挂入延时链表，到期返回 ROS_TIMEOUT
 * 中断上下文中调用会返回 ROS_FORBIDDEN_IN_ISR（并触发断言）。
 */
int ros_block_current(ros_wait_queue_t *wait_queue, uint32_t timeout);

/*
 * 与 ros_block_current() 等价，但要求调用者已持有临界区。
 * 入等待链、挂延时表、选下一个任务都在同一临界区完成，函数返回前已退出
 * 临界区并完成任务切换（被唤醒后从这里返回）。
 * 供互斥量等需要「状态修改 + 入链 + 选任务」原子的对象复用。
 */
void ros_block_current_locked(ros_wait_queue_t *wait_queue, uint32_t timeout);

/*
 * 「检查条件 → 阻塞 → 重试」循环的等待一步，配合 deadline 实现可续等超时。
 *   deadline : 首次调用的 ros_tick_get() + timeout
 * 返回 ROS_OK 表示应重新检查条件；其它值（ROS_TIMEOUT /
 * ROS_FORBIDDEN_IN_ISR）由调用方直接作为本次 API 的返回值。
 */
int ros_wait_retry(ros_wait_queue_t *queue, uint32_t deadline, uint32_t timeout);

/* 唤醒任务：摘除等待/延时节点，写入结果并加入就绪表 */
void ros_task_wake(ros_task_t *task, int result);

/*
 * 销毁对象前的统一收尾：等待队列均空时在临界区内把 *initialized 置 false。
 * 返回 ROS_BUSY 表示仍有等待者，调用方不得释放内存。
 */
int ros_obj_destroy_begin(bool *initialized,
                          ros_wait_queue_t *wait1,
                          ros_wait_queue_t *wait2);

#ifdef __cplusplus
}
#endif

#endif /* ROS_WAIT_H */
