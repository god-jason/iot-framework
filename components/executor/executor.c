/*
 * 执行器实现：把一个 planner_plan_t 中的 Action 顺序推进到结束。
 *
 * 算法流程（executor_process）：
 *   1) 未运行或已暂停时直接返回，不消耗任何 Action 进度；
 *   2) 循环处理当前 Action：IDLE 则先 start，再 update；
 *   3) 成功则 current_action++ 继续下一个；失败则整计划失败并清零 running；
 *   4) 其余状态（RUNNING/IDLE 等）本帧让出，等待下次调度；
 *   5) 全部 Action 完成后把 plan 置 SUCCESS 并累计 completed_count。
 *
 * 边界与注意事项：
 *   - executor 只做“调度驱动”，不负责 Action 内部的资源与取消；
 *   - 计数只在 plan 终止（成功/失败）时累加一次，stop 不计入任何计数；
 *   - 同一帧内可连续推进多个瞬时完成的 Action，因此调用频率不等于 Action 执行次数。
 */
#include "executor.h"

/*
 * 初始化执行器：清空绑定、运行标志、暂停标志与统计计数。
 * @param executor 执行器对象
 * @return 0；参数为 NULL 返回 -1
 */
int executor_init(executor_t *executor)
{
    if (executor == NULL) {
        return -1;
    }
    executor->current_plan = NULL;
    executor->running = false;
    executor->paused = false;
    executor->completed_count = 0;
    executor->failed_count = 0;
    return 0;
}

/*
 * 绑定计划并启动：plan 从头开始（current_action=0），状态置 RUNNING。
 * @param executor 执行器对象
 * @param plan     待执行计划
 * @return 0；任一参数为 NULL 返回 -1
 * @note 重复 start 会覆盖旧 plan，旧 plan 不会被自动停止。
 */
int executor_start(executor_t *executor, planner_plan_t *plan)
{
    if (executor == NULL || plan == NULL) {
        return -1;
    }
    executor->current_plan = plan;
    executor->running = true;
    executor->paused = false;
    plan->current_action = 0;
    plan->state = PLANNER_PLAN_RUNNING;
    return 0;
}

/*
 * 推进计划执行：顺序启动并更新 Action，直到遇到未完成项或计划结束。
 * @param executor 执行器对象
 * @return 0 正常处理；-1 参数为 NULL 或未绑定 plan
 * @note 未运行/暂停时静默返回 0，便于上层无条件周期调用。
 */
int executor_process(executor_t *executor)
{
    planner_plan_t *plan;

    /* 未启动或已暂停：本帧不做任何推进 */
    if (executor == NULL || !executor->running || executor->paused) {
        return 0;
    }
    plan = executor->current_plan;
    if (plan == NULL) {
        return -1;
    }

    while (plan->current_action < plan->action_count) {
        action_t *a = plan->actions[plan->current_action];
        if (action_get_state(a) == ACTION_IDLE) {
            action_start(a);        /* 首次进入该 Action：先启动 */
        }
        action_update(a);           /* 推进一个周期 */

        switch (action_get_state(a)) {
        case ACTION_SUCCESS:
            plan->current_action++;     /* 当前 Action 完成：切换到下一个 */
            break;
        case ACTION_FAILED:
            /* 任一 Action 失败即终止整个计划 */
            plan->state = PLANNER_PLAN_FAILED;
            executor->failed_count++;
            executor->running = false;
            return 0;
        default:
            /* RUNNING（或其他非终态）：本帧让出 CPU，下个周期继续 */
            return 0;
        }
    }

    /* 所有 Action 均成功：计划完成 */
    plan->state = PLANNER_PLAN_SUCCESS;
    executor->completed_count++;
    executor->running = false;
    return 0;
}

/*
 * 暂停执行：仅置暂停标志，保留 Action 与 plan 的全部进度。
 * @param executor 执行器对象
 * @return 0；参数为 NULL 返回 -1
 */
int executor_pause(executor_t *executor)
{
    if (executor == NULL) {
        return -1;
    }
    executor->paused = true;
    return 0;
}

/*
 * 恢复执行：清除暂停标志，下次 process 从暂停点继续。
 * @param executor 执行器对象
 * @return 0；参数为 NULL 返回 -1
 * @note 不会主动置 running，若之前已 stop 需重新 start。
 */
int executor_resume(executor_t *executor)
{
    if (executor == NULL) {
        return -1;
    }
    executor->paused = false;
    return 0;
}

/*
 * 停止执行：清除运行/暂停标志，并把绑定的 plan 回退为 PENDING。
 * @param executor 执行器对象
 * @return 0；参数为 NULL 返回 -1
 * @note 不回退 plan->current_action，也不通知 Action 取消；保留 plan 绑定关系。
 */
int executor_stop(executor_t *executor)
{
    if (executor == NULL) {
        return -1;
    }
    executor->running = false;
    executor->paused = false;
    if (executor->current_plan != NULL) {
        executor->current_plan->state = PLANNER_PLAN_PENDING;
    }
    return 0;
}

/*
 * 获取当前 Action（只读）。
 * @param executor 执行器对象
 * @return 当前 Action；未绑定 plan 或 current_action 越界（已执行完）返回 NULL
 */
action_t *executor_get_current_action(executor_t *executor)
{
    planner_plan_t *plan;
    if (executor == NULL) {
        return NULL;
    }
    plan = executor->current_plan;
    if (plan == NULL || plan->current_action >= plan->action_count) {
        return NULL;
    }
    return plan->actions[plan->current_action];
}