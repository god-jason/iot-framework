/*
 * planner_test.c - 计划器测试（对应 planner.c）
 *
 * 覆盖：注册/注销、启动/停止、按 action 顺序推进、
 *       完成与失败状态、当前计划查询、非法参数。
 */

#include "test.h"
#include "planner.h"

typedef struct {
    int update_result;
    int updates;
} plan_ctx_t;

static int step_start(action_t *a)
{
    (void)a;
    return 0;
}

static int step_update(action_t *a)
{
    plan_ctx_t *c = (plan_ctx_t *)a->ctx;
    c->updates++;
    return c->update_result;
}

void ut_robot_planner(void)
{
    plan_ctx_t c1;
    plan_ctx_t c2;
    action_t a1;
    action_t a2;
    action_t *actions[2];
    planner_plan_t plan;

    memset(&c1, 0, sizeof(c1));
    memset(&c2, 0, sizeof(c2));
    memset(&a1, 0, sizeof(a1));
    memset(&a2, 0, sizeof(a2));

    a1.name = "a1";
    a1.start = step_start;
    a1.update = step_update;
    a1.ctx = &c1;
    a2.name = "a2";
    a2.start = step_start;
    a2.update = step_update;
    a2.ctx = &c2;

    actions[0] = &a1;
    actions[1] = &a2;

    memset(&plan, 0, sizeof(plan));
    plan.id = 1U;
    plan.name = "plan";
    plan.actions = actions;
    plan.action_count = 2U;

    tf_begin("planner/注册与校验");

    TF_CHECK_INT(planner_init(), 0);
    TF_CHECK(planner_get_current() == NULL);

    TF_CHECK_INT(planner_add(NULL), -1);
    {
        planner_plan_t bad;

        memset(&bad, 0, sizeof(bad));
        bad.id = 2U;
        TF_CHECK_INT(planner_add(&bad), -1);      /* 无 action */
    }
    TF_CHECK_INT(planner_add(&plan), 0);
    TF_CHECK_INT(planner_add(&plan), 0);          /* 未做 id 去重 */

    tf_end();

    tf_begin("planner/执行推进");

    action_init(&a1);
    action_init(&a2);

    TF_CHECK_INT(planner_start(99U), -1);
    TF_CHECK_INT(planner_start(1U), 0);
    TF_CHECK(planner_get_current() == &plan);
    TF_CHECK_INT(plan.state, PLANNER_PLAN_RUNNING);

    /* action1 未完成：本帧停在 action1 */
    c1.update_result = 0;
    TF_CHECK_INT(planner_process(), 0);
    TF_CHECK_INT(c1.updates, 1);
    TF_CHECK_INT(c2.updates, 0);
    TF_CHECK_INT(plan.state, PLANNER_PLAN_RUNNING);

    /* action1 完成后进入 action2 */
    c1.update_result = 1;
    TF_CHECK_INT(planner_process(), 0);
    TF_CHECK_INT(c1.updates, 2);
    TF_CHECK_INT(c2.updates, 1);

    /* action2 完成：计划成功 */
    c2.update_result = 1;
    TF_CHECK_INT(planner_process(), 0);
    TF_CHECK_INT(plan.state, PLANNER_PLAN_SUCCESS);

    tf_end();

    tf_begin("planner/失败与停止");

    action_reset(&a1);
    action_reset(&a2);
    c1.updates = 0;
    c2.updates = 0;
    c1.update_result = -1;                        /* action1 失败 */
    c2.update_result = 0;

    TF_CHECK_INT(planner_start(1U), 0);
    TF_CHECK_INT(planner_process(), 0);
    TF_CHECK_INT(plan.state, PLANNER_PLAN_FAILED);

    TF_CHECK_INT(planner_stop(1U), 0);
    TF_CHECK_INT(plan.state, PLANNER_PLAN_PENDING);
    TF_CHECK(planner_get_current() == NULL);
    TF_CHECK_INT(planner_stop(99U), -1);

    /* 无当前计划时 process 返回 0 */
    TF_CHECK_INT(planner_process(), 0);

    tf_end();

    tf_begin("planner/注销");

    TF_CHECK_INT(planner_remove(1U), 0);
    TF_CHECK(planner_get_current() == NULL);

    tf_end();
}
