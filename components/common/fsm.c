/*
 * fsm.c - 有限状态机实现
 *
 * 模块职责：
 *   依据调用者提供的“状态表 + 转换表”执行状态机逻辑：
 *   收到事件后先调用当前状态的 action，再查找匹配的转换；
 *   转换时依次执行旧状态 exit、切换 current、新状态 entry。
 *   模块不持有事件队列，事件由调用者同步传入 dispatch。
 *
 * 数据流与依赖：
 *   states / transitions 数组均由调用者静态定义，本模块只保存指针，
 *   不做拷贝、不分配内存；因此这些数组的生命周期必须覆盖状态机。
 *   依赖 标准头文件。
 *
 * 边界约束：
 *   - 状态与事件的 ID 只做线性查找，状态数/转换数受 uint16_t 限制。
 *   - dispatch 中一旦命中某条转换：guard 为 false 时直接返回 0，
 *     不会再尝试后续同事件的其他转换；guard 为 NULL 视为无条件通过。
 *   - 未命中任何转换视为正常（返回 0），不属于错误。
 *   - 非线程安全，dispatch 与外部状态操作需串行化。
 */

#include "fsm.h"

/*
 * 按 ID 线性查找状态定义。
 * 参数：fsm - 状态机（states/state_count 必须已初始化）；id - 状态 ID
 * 返回：匹配的状态指针；未找到返回 NULL
 */
static const fsm_state_t *fsm_find_state(const fsm_t *fsm, uint32_t id)
{
    uint16_t i;
    for (i = 0; i < fsm->state_count; i++) {
        if (fsm->states[i].id == id) {
            return &fsm->states[i];
        }
    }
    return NULL;
}

/*
 * 初始化状态机（不进入任何状态）。
 * 参数：
 *   fsm              - 状态机对象（不可为 NULL）
 *   states           - 状态表（不可为 NULL，需长期有效）
 *   state_count      - 状态个数（> 0）
 *   transitions      - 转换表，可为 NULL（transition_count 应为 0）
 *   transition_count - 转换条目数
 *   ctx              - 用户上下文，透传给各回调
 * 返回：0 成功；-1 参数非法
 * 注意：需再调用 fsm_start() 才会进入运行态并执行 entry。
 */
int fsm_init(fsm_t *fsm,
                 const fsm_state_t *states, uint16_t state_count,
                 const fsm_transition_t *transitions, uint16_t transition_count,
                 void *ctx)
{
    if (fsm == NULL || states == NULL || state_count == 0) {
        return -1;
    }
    fsm->states = states;
    fsm->state_count = state_count;
    fsm->transitions = transitions;
    fsm->transition_count = transition_count;
    fsm->current = NULL;
    fsm->ctx = ctx;
    fsm->running = false;
    fsm->paused = false;
    return 0;
}

/*
 * 启动状态机并进入指定初始状态。
 * 参数：fsm - 状态机；initial_state - 初始状态 ID（必须存在于状态表）
 * 返回：0 成功；-1 参数非法或状态不存在
 * 注意：会复位 paused 标志，并执行初始状态的 entry 回调。
 */
int fsm_start(fsm_t *fsm, uint32_t initial_state)
{
    const fsm_state_t *s;
    if (fsm == NULL) {
        return -1;
    }
    s = fsm_find_state(fsm, initial_state);
    if (s == NULL) {
        return -1;
    }
    fsm->current = s;
    fsm->running = true;
    fsm->paused = false;
    if (s->entry != NULL) {
        s->entry(fsm, s->ctx);
    }
    return 0;
}

/*
 * 停止状态机。
 * 参数：fsm - 状态机
 * 返回：0 成功；-1 fsm 为 NULL
 * 注意：仅在运行中且有当前状态时执行 exit 回调；停止后 current 保留，
 *       便于查询最后状态，但 dispatch 会被拒绝。
 */
int fsm_stop(fsm_t *fsm)
{
    if (fsm == NULL) {
        return -1;
    }
    if (fsm->running && fsm->current != NULL && fsm->current->exit != NULL) {
        fsm->current->exit(fsm, fsm->current->ctx);
    }
    fsm->running = false;
    fsm->paused = false;
    return 0;
}

/*
 * 暂停状态机：暂停后 dispatch 直接失败，不执行 action/转换。
 * 参数：fsm - 状态机
 * 返回：0 成功；-1 fsm 为 NULL
 * 注意：不触发 exit，恢复时也不会重复 entry。
 */
int fsm_pause(fsm_t *fsm)
{
    if (fsm == NULL) {
        return -1;
    }
    fsm->paused = true;
    return 0;
}

/*
 * 恢复状态机（仅清除 paused 标志）。
 * 参数：fsm - 状态机
 * 返回：0 成功；-1 fsm 为 NULL
 * 注意：不检查 running 标志，未启动时调用仅清除暂停位。
 */
int fsm_resume(fsm_t *fsm)
{
    if (fsm == NULL) {
        return -1;
    }
    fsm->paused = false;
    return 0;
}

/*
 * 向状态机派发一个事件（状态机核心）。
 * 参数：fsm - 状态机；event - 事件 ID；data - 事件附带数据（透传给回调）
 * 返回：0 处理完成（含无匹配转换、guard 拒绝的情况）；
 *       -1 参数非法、未启动、已暂停或目标状态不存在
 * 执行顺序：
 *   1) 当前状态的 action（可为 NULL）；
 *   2) 线性查找 from == 当前状态 且 event 匹配的转换；
 *   3) guard 校验，false 则本次事件结束；
 *   4) 旧状态 exit -> 切换 current -> 新状态 entry。
 * 注意：只尝试第一条命中的转换；action 中修改 current 不会影响本次查找
 *       （查找基于已保存的 cur 快照）。
 */
int fsm_dispatch(fsm_t *fsm, uint32_t event, void *data)
{
    const fsm_state_t *cur;
    uint16_t i;

    if (fsm == NULL || fsm->current == NULL) {
        return -1;
    }
    if (!fsm->running || fsm->paused) {
        return -1;
    }

    cur = fsm->current;   /* 保存快照，避免回调中修改影响后续查找 */

    /* 1. 执行当前状态 action */
    if (cur->action != NULL) {
        cur->action(fsm, cur->ctx, event, data);
    }

    /* 2. 查找匹配的事件转换 */
    for (i = 0; i < fsm->transition_count; i++) {
        const fsm_transition_t *t = &fsm->transitions[i];
        const fsm_state_t *next;

        if (t->from != cur->id || t->event != event) {
            continue;
        }
        /* 3. guard 判断 */
        if (t->guard != NULL && !t->guard(fsm, cur->ctx, event, data)) {
            return 0;   /* 守卫拒绝：本次事件不转换 */
        }

        next = fsm_find_state(fsm, t->to);
        if (next == NULL) {
            return -1;       /* 转换表指向了不存在的状态 */
        }

        /* 4. 当前状态 exit */
        if (cur->exit != NULL) {
            cur->exit(fsm, cur->ctx);
        }
        /* 5. 切换状态 */
        fsm->current = next;
        /* 6. 新状态 entry */
        if (next->entry != NULL) {
            next->entry(fsm, next->ctx);
        }
        return 0;
    }

    return 0;   /* 无匹配转换：事件被忽略，属正常情况 */
}

/*
 * 强制切换到指定状态（绕过转换表与 guard）。
 * 参数：fsm - 状态机；state_id - 目标状态 ID
 * 返回：0 成功；-1 参数非法、无当前状态或目标状态不存在
 * 注意：执行当前状态 exit 与目标状态 entry；不检查 running/paused，
 *       可用于异常恢复或初始化跳转。
 */
int fsm_transition(fsm_t *fsm, uint32_t state_id)
{
    const fsm_state_t *next;
    if (fsm == NULL || fsm->current == NULL) {
        return -1;
    }
    next = fsm_find_state(fsm, state_id);
    if (next == NULL) {
        return -1;
    }
    if (fsm->current->exit != NULL) {
        fsm->current->exit(fsm, fsm->current->ctx);
    }
    fsm->current = next;
    if (next->entry != NULL) {
        next->entry(fsm, next->ctx);
    }
    return 0;
}

/*
 * 获取当前状态 ID。
 * 参数：fsm - 状态机
 * 返回：当前状态 ID；未启动或无当前状态时返回 0（0 也可能为合法 ID）
 * 注意：实现位于 fsm.h，作为 static inline 提供。
 */
