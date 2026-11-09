/*
 * iot_stats.c - 运行时统计
 *
 * 数据来源：
 *   - 切换次数：sched.c 在每次 iot_sched_switch / iot_sched_from_isr 累加；
 *   - 运行 tick：tick.c 在推进时间时累加到当前任务；
 *   - 栈余量：统计栈底连续保持魔数的字节数。
 *
 * 任务注册表由 iot_task.c 维护，iot_stats_reset() 会遍历它清零
 * 每个任务的 run_ticks / switches；也可按 TCB 指针单独查询。
 */

#include "iot.h"
#include "iot_port.h"

#include <string.h>

static uint32_t iot_stack_free_bytes(const iot_task_t *task)
{
#if IOT_USE_STACK_CHECK
    const uint32_t *words;
    uint32_t total;
    uint32_t i;

    if (task == NULL || task->stack_mem == NULL) {
        return 0U;
    }

    total = task->stack_size / (uint32_t)sizeof(uint32_t);
    words = (const uint32_t *)task->stack_mem;

    for (i = 0U; i < total; i++) {
        if (words[i] != IOT_STACK_MAGIC) {
            break;
        }
    }
    return i * (uint32_t)sizeof(uint32_t);
#else
    (void)task;
    return 0U;
#endif
}

int iot_stats_get(iot_stats_t *out)
{
    if (out == NULL) {
        return IOT_INVALID_PARAM;
    }

#if IOT_USE_STATS
    out->ctx_switches = iot_sched_switch_count();
#else
    out->ctx_switches = 0U;
#endif
    out->tick_count = iot_tick_get();
    return IOT_OK;
}

int iot_task_get_stats(const iot_task_t *task, iot_task_stats_t *out)
{
    if (task == NULL || out == NULL) {
        return IOT_INVALID_PARAM;
    }

    memset(out, 0, sizeof(*out));

#if IOT_USE_STATS
    out->run_ticks = task->run_ticks;
    out->switches = task->switches;
#endif

    out->stack_free_bytes = iot_stack_free_bytes(task);
    return IOT_OK;
}

void iot_stats_reset(void)
{
#if IOT_USE_STATS
    iot_sched_reset_switch_count();
    iot_task_reset_stats_all();
#endif
}
