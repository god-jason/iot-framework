#ifndef IOT_STATS_H
#define IOT_STATS_H

/*
 * iot_stats.h - 运行时统计（stats.c）
 *
 * 提供内核级与任务级运行数据：上下文切换次数、每任务累计运行 tick、
 * 被调度次数、栈剩余空间。数据在 tick/switch 路径上累加，开销为常数。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct iot_stats {
    uint32_t ctx_switches;      /* 上下文切换总次数 */
    uint32_t tick_count;        /* 内核 tick 计数 */
} iot_stats_t;

typedef struct iot_task_stats {
    uint32_t run_ticks;         /* 累计运行 tick */
    uint32_t switches;          /* 被调度上 CPU 的次数 */
    uint32_t stack_free_bytes;  /* 从栈底起连续保持魔数的字节数（栈水位） */
} iot_task_stats_t;

/* 读取内核级统计；out 为 NULL 时返回 IOT_INVALID_PARAM */
int iot_stats_get(iot_stats_t *out);

/* 读取任务级统计 */
int iot_task_get_stats(const iot_task_t *task, iot_task_stats_t *out);

/*
 * 任务 CPU 占用率，单位千分比（0..1000，1000 = 100.0%）。
 * 统计窗口为内核启动至今的 tick 总数（含空闲任务），各任务占比之和
 * 约为 100%。未启用统计、task 为 NULL 或 tick 计数为 0 时返回 0。
 */
uint32_t iot_task_cpu_permille(const iot_task_t *task);

/* 重置内核计数与所有任务的运行统计 */
void iot_stats_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* IOT_STATS_H */
