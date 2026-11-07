#ifndef EXECUTOR_H
#define EXECUTOR_H

/*
 * 执行器：驱动单个 plan（顺序执行其 Action），带计数与启停控制。
 *
 * 模块职责：planner 负责“有哪些 plan、当前该跑哪个”，executor 负责“把选中的 plan
 * 逐个 Action 推进到结束”，并在成功/失败时更新计数与 plan 状态。
 *
 * 执行语义：
 *   - 每次 executor_process() 最多推进整个 plan，直到某 Action 返回 RUNNING（本帧让出）；
 *   - Action 处于 IDLE 时先自动 action_start()，随后 action_update()；
 *   - 全部 Action 成功 -> plan 置 SUCCESS、completed_count++、running 置 false；
 *   - 任一 Action 失败 -> plan 置 FAILED、failed_count++、running 置 false。
 *
 * 注意事项：
 *   - 不持有 plan 的所有权，units 由使用方提供并保证生命周期；
 *   - 同一 executor 只能绑定一个 plan，切换 plan 需重新 executor_start；
 *   - 非线程安全。
 */

#include "planner.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 执行器上下文 */
typedef struct {
    planner_plan_t *current_plan;   /* 当前绑定的计划，NULL 表示未绑定 */

    bool running;                   /* 是否处于运行中（start 后置位，成功/失败/stop 后清零） */
    bool paused;                    /* 是否暂停：为 true 时 process 直接返回，不推进 Action */

    uint32_t completed_count;       /* 累计成功完成的 plan 数（init 时清零） */
    uint32_t failed_count;          /* 累计失败的 plan 数（init 时清零） */
} executor_t;

/*
 * 初始化执行器：解绑 plan、清除运行/暂停标志并清零计数器。
 * @param executor 执行器对象
 * @return 0；executor 为 NULL 返回 -1
 */
int executor_init(executor_t *executor);

/*
 * 绑定并启动一个计划：从头开始执行（current_action 归零，plan 置 RUNNING）。
 * @param executor 执行器对象
 * @param plan     待执行计划，不可为 NULL，且其 actions 需已就绪
 * @return 0；参数为 NULL 返回 -1
 * @note 会覆盖 executor 之前绑定的 plan，不负责停止旧 plan。
 */
int executor_start(executor_t *executor, planner_plan_t *plan);

/*
 * 推进一个调度周期：顺序执行当前 plan 的 Action。
 * @param executor 执行器对象
 * @return 0 正常处理（含未运行/暂停/本帧未完成等情况）；-1 参数为 NULL 或未绑定 plan
 * @note 内部可能一次推进多个已完成的 Action，遇到 RUNNING 才返回。
 */
int executor_process(executor_t *executor);

/*
 * 暂停执行：置 paused，process 不再推进，但保留当前 Action 的进度。
 * @return 0；参数为 NULL 返回 -1
 */
int executor_pause(executor_t *executor);

/*
 * 恢复执行：清除 paused，下次 process 继续推进。
 * @return 0；参数为 NULL 返回 -1
 */
int executor_resume(executor_t *executor);

/*
 * 停止执行：清除 running/paused，并把当前 plan 状态回退为 PENDING。
 * @return 0；参数为 NULL 返回 -1
 * @note 不会 cancel 正在运行的 Action，也不会解绑 plan。
 */
int executor_stop(executor_t *executor);

/*
 * 获取当前正在执行的 Action。
 * @param executor 执行器对象
 * @return 当前 Action 指针；未绑定 plan 或已越界（执行完毕）时返回 NULL
 */
action_t *executor_get_current_action(executor_t *executor);

#ifdef __cplusplus
}
#endif

#endif /* EXECUTOR_H */