/*
 * 动作（Action）状态机实现。
 *
 * 职责：统一调度使用方提供的 start / update / cancel / reset 回调，
 * 维护 ACTION_IDLE → ACTION_RUNNING → SUCCESS/FAILED 的状态迁移。
 *
 * 设计要点：
 *   - 本文件不涉及具体业务与硬件，属于纯框架代码，所有副作用发生在回调中。
 *   - 回调缺省（NULL）时按“成功”语义处理：start/cancel/reset 视为 0，
 *     update 视为返回 1（立即完成）。
 *   - 边界约定：所有入口先做 NULL 检查并返回 -1；状态字段始终保持在枚举取值范围内。
 *   - 无动态内存、无全局变量、可重入（同一 action 对象需由单一调用者串行使用）。
 */
#include "action.h"

/*
 * 初始化动作对象：仅将状态复位为 IDLE。
 * @param action 动作对象
 * @return 0 成功；-1 参数为 NULL
 */
int action_init(action_t *action)
{
    if (action == NULL) {
        return -1;
    }
    action->state = ACTION_IDLE;
    return 0;
}

/*
 * 启动动作：调用 start 回调，成功则进入 RUNNING，失败则停留在 FAILED。
 * @param action 动作对象
 * @return 回调返回值；-1 参数为 NULL
 * @note 未检查当前状态，允许从任意状态重新启动（例如 FAILED 后直接重试）。
 */
int action_start(action_t *action)
{
    int ret;
    if (action == NULL) {
        return -1;
    }
    /* 回调可缺省：没有 start 视为启动成功 */
    ret = (action->start != NULL) ? action->start(action) : 0;
    if (ret == 0) {
        action->state = ACTION_RUNNING;
    } else {
        action->state = ACTION_FAILED;
    }
    return ret;
}

/*
 * 推进一个周期：仅当状态为 RUNNING 时调用 update 回调，并按返回值迁移状态。
 * @param action 动作对象
 * @return 0 表示调用已被接受并处理；-1 表示参数为 NULL 或状态非 RUNNING
 * @note 返回 0 不代表动作成功，最终结果需读取 state。
 */
int action_update(action_t *action)
{
    int ret;
    if (action == NULL) {
        return -1;
    }
    /* 只有运行中的动作才允许被推进，避免重复触发已结束的动作 */
    if (action->state != ACTION_RUNNING) {
        return -1;
    }
    /* update 缺省时返回 1，表示“本次 tick 即完成” */
    ret = (action->update != NULL) ? action->update(action) : 1;
    if (ret == 0) {
        action->state = ACTION_RUNNING;     /* 0：继续运行 */
    } else if (ret > 0) {
        action->state = ACTION_SUCCESS;     /* 正数：正常完成 */
    } else {
        action->state = ACTION_FAILED;      /* 负数：执行失败 */
    }
    return 0;
}

/*
 * 取消动作：调用 cancel 回调（用于停止执行机构/释放资源），随后无条件置为 CANCELLED。
 * @param action 动作对象
 * @return cancel 回调的返回值；-1 参数为 NULL
 * @note 即使回调失败，状态仍为 CANCELLED，以保证不会继续调用 update。
 */
int action_cancel(action_t *action)
{
    int ret;
    if (action == NULL) {
        return -1;
    }
    ret = (action->cancel != NULL) ? action->cancel(action) : 0;
    action->state = ACTION_CANCELLED;
    return ret;
}

/*
 * 复位动作：调用 reset 回调清理内部进度，随后无条件回到 IDLE 以便复用。
 * @param action 动作对象
 * @return reset 回调的返回值；-1 参数为 NULL
 */
int action_reset(action_t *action)
{
    int ret;
    if (action == NULL) {
        return -1;
    }
    ret = (action->reset != NULL) ? action->reset(action) : 0;
    action->state = ACTION_IDLE;
    return ret;
}

/*
 * 查询动作状态（只读接口，NULL 安全）。
 * @param action 动作对象，可为 NULL
 * @return 当前状态；NULL 时返回 ACTION_IDLE
 */
action_state_e action_get_state(const action_t *action)
{
    return (action == NULL) ? ACTION_IDLE : action->state;
}