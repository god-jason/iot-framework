/*
 * 任务规划器实现：运行期注册表 + 单计划顺序执行。
 *
 * 算法流程（planner_process）：
 *   1) 无当前计划或状态非 RUNNING 时直接返回；
 *   2) 循环处理当前 Action：IDLE 则先 start，再 update；
 *   3) SUCCESS 则 current_action++，FAILED 则整计划置 FAILED 并解除当前指针；
 *   4) 遇到 RUNNING 等非终态时让出，等待下一拍；
 *   5) 所有 Action 完成则计划置 SUCCESS 并解除当前指针。
 *
 * 数据结构：
 *   - s_plans[]：计划指针注册表（容量 PLANNER_PLAN_MAX，首次使用时惰性分配），空槽为 NULL；
 *   - s_current：当前执行计划，同一时刻最多一个。
 *
 * 边界与注意事项：
 *   - 注册表满了 planner_add 返回 -1；
 *   - 只保存指针，不拷贝 plan/actions，使用方需保证其生命周期；
 *   - 计划结束后 s_current 被清空，状态保留在 plan 结构体内供查询；
 *   - 非线程安全。
 */
#include "planner.h"
#include "port_mem.h"

/* 注册表容量：可同时登记的计划数上限 */
#define PLANNER_PLAN_MAX 8

static planner_plan_t **s_plans = NULL;   /* 计划注册表（惰性分配），空槽为 NULL */
static planner_plan_t *s_current = NULL;  /* 当前执行的计划 */

/*
 * 惰性分配计划注册表；已分配则直接复用，不重复分配。
 * @return 0 成功（已分配或本次分配成功）；-1 分配失败
 */
static int planner_ensure(void)
{
    if (s_plans != NULL) {
        return 0;
    }
    s_plans = (planner_plan_t **)port_calloc(PLANNER_PLAN_MAX, sizeof(*s_plans));
    return (s_plans != NULL) ? 0 : -1;
}

/*
 * 清空注册表与当前计划指针。
 * @return 0 成功；-1 注册表分配失败
 * @note 仅丢弃指针，不修改各 plan 自身状态。
 */
int planner_init(void)
{
    int i;
    if (planner_ensure() != 0) {
        return -1;                  /* 注册表不可用 */
    }
    for (i = 0; i < PLANNER_PLAN_MAX; i++) {
        s_plans[i] = NULL;
    }
    s_current = NULL;
    return 0;
}

/*
 * 按 id 线性查找已注册计划。
 * @return 计划指针；未找到返回 NULL
 * @note 若存在重复 id，返回第一个匹配项。
 */
static planner_plan_t *planner_find(uint32_t id)
{
    int i;
    if (s_plans == NULL) {
        return NULL;                    /* 注册表尚未分配 */
    }
    for (i = 0; i < PLANNER_PLAN_MAX; i++) {
        if (s_plans[i] != NULL && s_plans[i]->id == id) {
            return s_plans[i];
        }
    }
    return NULL;
}

/*
 * 注册计划：放入第一个空槽，并初始化为 PENDING、进度 0。
 * @param plan 计划对象，actions 必须非 NULL 且 action_count > 0
 * @return 0 成功；-1 参数非法或注册表已满
 * @note 不做 id 唯一性校验。
 */
int planner_add(planner_plan_t *plan)
{
    int i;
    if (plan == NULL || plan->actions == NULL || plan->action_count == 0) {
        return -1;                      /* 非法计划：没有可执行的 Action */
    }
    if (planner_ensure() != 0) {
        return -1;                      /* 注册表分配失败 */
    }
    for (i = 0; i < PLANNER_PLAN_MAX; i++) {
        if (s_plans[i] == NULL) {
            s_plans[i] = plan;
            break;                      /* 找到空槽即停止 */
        }
    }
    if (i == PLANNER_PLAN_MAX) {
        return -1;                      /* 注册表已满 */
    }
    plan->state = PLANNER_PLAN_PENDING;
    plan->current_action = 0;
    return 0;
}

/*
 * 注销计划：清空注册槽；若目标正是当前执行计划，一并清空 s_current。
 * @param id 计划标识
 * @return 0 成功；-1 未找到
 * @note 不修改 plan 自身字段，也不停止其内部 Action。
 */
int planner_remove(uint32_t id)
{
    int i;
    if (s_plans == NULL) {
        return -1;                      /* 注册表尚未分配 */
    }
    for (i = 0; i < PLANNER_PLAN_MAX; i++) {
        if (s_plans[i] != NULL && s_plans[i]->id == id) {
            if (s_current == s_plans[i]) {
                s_current = NULL;       /* 防止悬空指针 */
            }
            s_plans[i] = NULL;
            return 0;
        }
    }
    return -1;                          /* 未注册 */
}

/*
 * 启动计划：置 RUNNING、进度归零，并作为当前计划。
 * @param id 计划标识
 * @return 0 成功；-1 未找到
 * @note 若已有其他计划在执行，直接覆盖 s_current（旧计划状态不变）。
 */
int planner_start(uint32_t id)
{
    planner_plan_t *plan = planner_find(id);
    if (plan == NULL) {
        return -1;
    }
    plan->state = PLANNER_PLAN_RUNNING;
    plan->current_action = 0;
    s_current = plan;
    return 0;
}

/*
 * 停止计划：状态回退为 PENDING；若是当前计划则解除。
 * @param id 计划标识
 * @return 0 成功；-1 未找到
 * @note 保留 current_action 与 Action 状态，再次 start 会从头开始。
 */
int planner_stop(uint32_t id)
{
    planner_plan_t *plan = planner_find(id);
    if (plan == NULL) {
        return -1;
    }
    plan->state = PLANNER_PLAN_PENDING;
    if (s_current == plan) {
        s_current = NULL;
    }
    return 0;
}

/*
 * 获取当前计划。
 * @return 当前计划指针；无则 NULL
 */
planner_plan_t *planner_get_current(void)
{
    return s_current;
}

/*
 * 推进当前计划：顺序启动并更新 Action，直到遇到未完成项或计划结束。
 * @return 0（无当前计划/状态非 RUNNING 时也返回成功，便于周期调用）
 * @note 计划结束（成功或失败）后会把 s_current 置 NULL。
 */
int planner_process(void)
{
    planner_plan_t *plan = s_current;

    /* 没有可执行计划：静默返回 */
    if (plan == NULL || plan->state != PLANNER_PLAN_RUNNING) {
        return 0;
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
            s_current = NULL;
            return 0;
        default:
            /* RUNNING：等待下一拍 */
            return 0;
        }
    }

    /* 全部 Action 完成 */
    plan->state = PLANNER_PLAN_SUCCESS;
    s_current = NULL;
    return 0;
}