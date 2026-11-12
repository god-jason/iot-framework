/*
 * ros_stats.c - 运行时统计
 *
 * 数据来源：
 *   - 切换次数：sched.c 在每次 ros_sched_switch / ros_sched_from_isr 累加；
 *   - 运行 tick：tick.c 在推进时间时累加到当前任务；
 *   - 栈余量：统计栈底连续保持魔数的字节数。
 *
 * 任务注册表由 ros_task.c 维护，ros_stats_reset() 会遍历它清零
 * 每个任务的 run_ticks / switches；也可按 TCB 指针单独查询。
 */

#include "ros.h"
#include "ros_port.h"

#include <string.h>

/* 统计栈底连续保持魔数的字节数，即从未使用过的栈空间（栈水位）。 */
static uint32_t ros_stack_free_bytes(const ros_task_t *task)
{
#if ROS_USE_STACK_CHECK
    const uint32_t *words;
    uint32_t total;
    uint32_t i;

    if (task == NULL || task->stack_mem == NULL) {
        return 0U;
    }

    total = task->stack_size / (uint32_t)sizeof(uint32_t);
    words = (const uint32_t *)task->stack_mem;

    for (i = 0U; i < total; i++) {
        if (words[i] != ROS_STACK_MAGIC) {
            break;
        }
    }
    return i * (uint32_t)sizeof(uint32_t);
#else
    (void)task;
    return 0U;
#endif
}

/* 汇总内核统计：tick 计数、上下文切换次数、任务数量等。 */
int ros_stats_get(ros_stats_t *out)
{
    if (out == NULL) {
        return ROS_INVALID_PARAM;
    }

#if ROS_USE_STATS
    out->ctx_switches = ros_sched_switch_count();
#else
    out->ctx_switches = 0U;
#endif
    out->tick_count = ros_tick_get();
    return ROS_OK;
}

/* 读取指定任务的累计运行 tick、被调度次数与栈余量。 */
int ros_task_get_stats(const ros_task_t *task, ros_task_stats_t *out)
{
    if (task == NULL || out == NULL) {
        return ROS_INVALID_PARAM;
    }

    memset(out, 0, sizeof(*out));

#if ROS_USE_STATS
    out->run_ticks = task->run_ticks;
    out->switches = task->switches;
#endif

    out->stack_free_bytes = ros_stack_free_bytes(task);
    return ROS_OK;
}

/* 任务 CPU 占用率（千分比）：run_ticks / 系统 tick 总数。 */
uint32_t ros_task_cpu_permille(const ros_task_t *task)
{
#if ROS_USE_STATS
    uint32_t total;

    if (task == NULL) {
        return 0U;
    }
    total = ros_tick_get();
    if (total == 0U) {
        return 0U;
    }
    return (uint32_t)(((uint64_t)task->run_ticks * 1000ULL) / total);
#else
    (void)task;
    return 0U;
#endif
}

/* 清零全局切换计数与所有任务的运行统计。 */
void ros_stats_reset(void)
{
#if ROS_USE_STATS
    ros_sched_reset_switch_count();
    ros_task_reset_stats_all();
#endif
}
