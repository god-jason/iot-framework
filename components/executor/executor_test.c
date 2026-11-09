/*
 * executor_test.c - 执行器测试（对应 executor.c）
 *
 * 覆盖：绑定启动、顺序推进、暂停/恢复、停止、计数统计、
 *       当前动作查询、非法参数。
 */

#include "test.h"
#include "executor.h"

typedef struct {
    int update_result;
} exec_ctx_t;

static int step_start(action_t *a)
{
    (void)a;
    return 0;
}

static int step_update(action_t *a)
{
    return ((exec_ctx_t *)a->ctx)->update_result;
}

void ut_robot_executor(void)
{
    exec_ctx_t c1;
    exec_ctx_t c2;
    action_t a1;
    action_t a2;
    action_t *actions[2];
    planner_plan_t plan;
    executor_t ex;

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
    plan.actions = actions;
    plan.action_count = 2U;

    action_init(&a1);
    action_init(&a2);

    tf_begin("executor/初始化与启动");

    TF_CHECK_INT(executor_init(NULL), -1);
    TF_CHECK_INT(executor_init(&ex), 0);
    TF_CHECK(!ex.running);
    TF_CHECK_INT(ex.completed_count, 0);
    TF_CHECK_INT(ex.failed_count, 0);

    TF_CHECK_INT(executor_start(NULL, &plan), -1);
    TF_CHECK_INT(executor_start(&ex, NULL), -1);
    TF_CHECK_INT(executor_start(&ex, &plan), 0);
    TF_CHECK(ex.running);
    TF_CHECK_INT(plan.state, PLANNER_PLAN_RUNNING);
    TF_CHECK_INT(plan.current_action, 0);
    TF_CHECK(executor_get_current_action(&ex) == &a1);

    /* 未运行状态下 process 返回 0，不做任何推进 */
    TF_CHECK_INT(executor_process(&ex), 0);

    tf_end();

    tf_begin("executor/推进与完成");

    c1.update_result = 1;         /* 第一个动作立即完成 */
    c2.update_result = 0;         /* 第二个动作继续 */
    TF_CHECK_INT(executor_process(&ex), 0);
    TF_CHECK(executor_get_current_action(&ex) == &a2);
    TF_CHECK_INT(plan.current_action, 1);

    c2.update_result = 1;
    TF_CHECK_INT(executor_process(&ex), 0);
    TF_CHECK_INT(plan.state, PLANNER_PLAN_SUCCESS);
    TF_CHECK(!ex.running);
    TF_CHECK_INT(ex.completed_count, 1);
    TF_CHECK(executor_get_current_action(&ex) == NULL);

    /* 未运行时 process 直接返回 */
    TF_CHECK_INT(executor_process(&ex), 0);

    tf_end();

    tf_begin("executor/暂停与恢复");

    action_reset(&a1);
    action_reset(&a2);
    c1.update_result = 0;
    c2.update_result = 0;

    TF_CHECK_INT(executor_start(&ex, &plan), 0);
    TF_CHECK_INT(executor_pause(&ex), 0);
    TF_CHECK(ex.paused);
    TF_CHECK_INT(executor_process(&ex), 0);
    TF_CHECK_INT(plan.current_action, 0);

    TF_CHECK_INT(executor_resume(&ex), 0);
    TF_CHECK(!ex.paused);
    TF_CHECK_INT(executor_process(&ex), 0);
    TF_CHECK_INT(plan.current_action, 0);

    TF_CHECK_INT(executor_pause(NULL), -1);
    TF_CHECK_INT(executor_resume(NULL), -1);

    tf_end();

    tf_begin("executor/失败与停止");

    c1.update_result = -1;
    action_reset(&a1);
    action_reset(&a2);
    TF_CHECK_INT(executor_start(&ex, &plan), 0);
    TF_CHECK_INT(executor_process(&ex), 0);
    TF_CHECK_INT(plan.state, PLANNER_PLAN_FAILED);
    TF_CHECK_INT(ex.failed_count, 1);

    action_reset(&a1);
    action_reset(&a2);
    TF_CHECK_INT(executor_start(&ex, &plan), 0);
    TF_CHECK_INT(executor_stop(&ex), 0);
    TF_CHECK(!ex.running);
    TF_CHECK_INT(plan.state, PLANNER_PLAN_PENDING);
    TF_CHECK_INT(executor_stop(NULL), -1);

    tf_end();
}
