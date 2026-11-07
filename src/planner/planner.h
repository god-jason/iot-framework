#ifndef PLANNER_H
#define PLANNER_H

/*
 * 任务规划器：按顺序执行一组 Action。
 *
 * 模块职责：维护有限个计划（plan）的静态注册表，并记录“当前正在执行的计划”。
 * 每个 plan 内部由 action 数组顺序组成，planner_process 负责推进当前 plan 的
 * 当前 Action；planner 本身不做调度决策（不按 priority 抢占）。
 *
 * 数据结构：
 *   - planner_plan_t：计划描述（id、优先级、Action 数组、进度、状态）；
 *   - 注册表容量 PLANNER_PLAN_MAX（见 .c，默认 8），存指针不拷贝。
 *
 * 注意事项：
 *   - plan 与其 actions 数组的生命周期由使用方保证，planner 不分配也不释放；
 *   - priority 字段当前仅保存，不参与调度；
 *   - 同一时刻最多一个 s_current（单计划执行模型），非线程安全。
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "action.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 计划状态 */
typedef enum {
    PLANNER_PLAN_PENDING,   /* 待执行：已注册但尚未启动，或被 stop 回退 */
    PLANNER_PLAN_RUNNING,   /* 执行中：由 planner_process 推进 */
    PLANNER_PLAN_SUCCESS,   /* 全部 Action 成功 */
    PLANNER_PLAN_FAILED     /* 任一 Action 失败 */
} planner_plan_state_t;

/* 计划描述：由使用方静态定义并注册到 planner */
typedef struct {
    uint32_t id;                    /* 计划唯一标识，planner_remove/start/stop 用 */
    const char *name;               /* 计划名称（调试/日志用，可为 NULL） */

    uint8_t priority;               /* 优先级（当前仅存储，未参与调度） */
    planner_plan_state_t state;     /* 当前状态，由 planner 维护 */

    action_t **actions;             /* Action 指针数组（外部持有），按数组顺序执行 */
    uint16_t action_count;          /* Action 个数，必须 >0 才能注册 */
    uint16_t current_action;        /* 当前执行到第几个 Action（下标） */

    void *ctx;                      /* 使用方私有数据，planner 不解释 */
} planner_plan_t;

/*
 * 清空计划注册表与当前计划指针。
 * @return 0
 */
int planner_init(void);

/*
 * 注册一个计划：占用一个空槽并重置为 PENDING、current_action=0。
 * @param plan 计划对象，actions 非 NULL 且 action_count > 0
 * @return 0 成功；-1 参数非法或注册表已满
 * @note 不做 id 去重检查，重复 id 时 find/remove 只命中第一个。
 */
int planner_add(planner_plan_t *plan);

/*
 * 按 id 注销计划；若它是当前执行计划，同时清空当前指针。
 * @param id 计划标识
 * @return 0 成功；-1 未找到
 */
int planner_remove(uint32_t id);

/*
 * 启动指定计划：置 RUNNING、进度归零，并设为当前计划。
 * @param id 计划标识
 * @return 0 成功；-1 未找到
 * @note 会替换掉之前正在执行的计划（旧计划状态不回退）。
 */
int planner_start(uint32_t id);

/*
 * 停止指定计划：状态回退为 PENDING；若是当前计划则清空当前指针。
 * @param id 计划标识
 * @return 0 成功；-1 未找到
 * @note 不回退 current_action，也不 cancel 内部 Action。
 */
int planner_stop(uint32_t id);

/*
 * 推进当前计划一个周期（内部逐个启动/更新 Action，直到遇到未完成项）。
 * @return 0 正常处理；无当前计划或状态非 RUNNING 时也返回 0
 */
int planner_process(void);

/*
 * 获取当前正在执行的计划。
 * @return 当前计划指针；无则返回 NULL
 */
planner_plan_t *planner_get_current(void);

#ifdef __cplusplus
}
#endif

#endif /* PLANNER_H */