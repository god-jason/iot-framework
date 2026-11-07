/* robot/executor 单元测试：单个计划的顺序执行、暂停/恢复/停止与计数。 */
#include "test.h"
#include "executor.h"

static int s_once_calls;

static int cb_success(action_t *a)
{
    (void)a;
    return 1;
}

static int cb_failure(action_t *a)
{
    (void)a;
    return -1;
}

static int cb_run_once(action_t *a)
{
    (void)a;
    s_once_calls++;
    return (s_once_calls >= 2) ? 1 : 0;
}

static void setup_action(action_t *a, int (*fn)(action_t *))
{
    memset(a, 0, sizeof(*a));
    action_init(a);
    a->update = fn;
}

void ut_robot_executor(void)
{
    executor_t ex;
    planner_plan_t plan;
    action_t a1;
    action_t a2;
    action_t *acts[2];
    action_t *cur;

    tf_begin("robot/executor 初始化");
    memset(&ex, 0xff, sizeof(ex));
    TF_CHECK_INT(executor_init(&ex), 0);
    TF_CHECK(ex.current_plan == NULL);
    TF_CHECK(!ex.running);
    TF_CHECK(!ex.paused);
    TF_CHECK_INT(ex.completed_count, 0);
    TF_CHECK_INT(ex.failed_count, 0);
    TF_CHECK_INT(executor_init(NULL), -1);
    tf_end();

    tf_begin("robot/executor 顺序执行两个动作");
    setup_action(&a1, cb_success);
    setup_action(&a2, cb_success);
    acts[0] = &a1;
    acts[1] = &a2;
    memset(&plan, 0, sizeof(plan));
    plan.actions = acts;
    plan.action_count = 2;

    executor_init(&ex);
    TF_CHECK_INT(executor_start(&ex, &plan), 0);
    TF_CHECK(ex.running);
    TF_CHECK(!ex.paused);
    TF_CHECK_INT(plan.current_action, 0);
    TF_CHECK_INT(plan.state, PLANNER_PLAN_RUNNING);
    cur = executor_get_current_action(&ex);
    TF_CHECK(cur == &a1);

    TF_CHECK_INT(executor_process(&ex), 0);
    TF_CHECK_INT(plan.state, PLANNER_PLAN_SUCCESS);
    TF_CHECK_INT(plan.current_action, 2);
    TF_CHECK_INT(ex.completed_count, 1);
    TF_CHECK(!ex.running);
    TF_CHECK(executor_get_current_action(&ex) == NULL);
    tf_end();

    tf_begin("robot/executor 运行中的动作分拍推进");
    s_once_calls = 0;
    setup_action(&a1, cb_run_once);
    acts[0] = &a1;
    memset(&plan, 0, sizeof(plan));
    plan.actions = acts;
    plan.action_count = 1;

    executor_init(&ex);
    executor_start(&ex, &plan);
    TF_CHECK_INT(executor_process(&ex), 0);
    TF_CHECK_INT(plan.state, PLANNER_PLAN_RUNNING);
    TF_CHECK_INT(plan.current_action, 0);
    TF_CHECK(ex.running);
    TF_CHECK_INT(action_get_state(&a1), ACTION_RUNNING);
    TF_CHECK(executor_get_current_action(&ex) == &a1);

    TF_CHECK_INT(executor_process(&ex), 0);
    TF_CHECK_INT(plan.state, PLANNER_PLAN_SUCCESS);
    TF_CHECK_INT(ex.completed_count, 1);
    TF_CHECK(!ex.running);
    tf_end();

    tf_begin("robot/executor 动作失败");
    setup_action(&a1, cb_failure);
    acts[0] = &a1;
    memset(&plan, 0, sizeof(plan));
    plan.actions = acts;
    plan.action_count = 1;

    executor_init(&ex);
    executor_start(&ex, &plan);
    TF_CHECK_INT(executor_process(&ex), 0);
    TF_CHECK_INT(plan.state, PLANNER_PLAN_FAILED);
    TF_CHECK_INT(plan.current_action, 0);
    TF_CHECK_INT(ex.failed_count, 1);
    TF_CHECK_INT(ex.completed_count, 0);
    TF_CHECK(!ex.running);
    tf_end();

    tf_begin("robot/executor 暂停/恢复/停止");
    setup_action(&a1, cb_success);
    acts[0] = &a1;
    memset(&plan, 0, sizeof(plan));
    plan.actions = acts;
    plan.action_count = 1;

    executor_init(&ex);
    executor_start(&ex, &plan);
    TF_CHECK_INT(executor_pause(&ex), 0);
    TF_CHECK(ex.paused);
    TF_CHECK_INT(executor_process(&ex), 0);
    TF_CHECK_INT(action_get_state(&a1), ACTION_IDLE);
    TF_CHECK_INT(plan.state, PLANNER_PLAN_RUNNING);

    TF_CHECK_INT(executor_resume(&ex), 0);
    TF_CHECK(!ex.paused);
    TF_CHECK_INT(executor_process(&ex), 0);
    TF_CHECK_INT(plan.state, PLANNER_PLAN_SUCCESS);

    setup_action(&a1, cb_success);
    executor_start(&ex, &plan);
    TF_CHECK_INT(executor_stop(&ex), 0);
    TF_CHECK(!ex.running);
    TF_CHECK(!ex.paused);
    TF_CHECK_INT(plan.state, PLANNER_PLAN_PENDING);
    tf_end();

    tf_begin("robot/executor 空闲状态与空计划");
    executor_init(&ex);
    TF_CHECK_INT(executor_process(&ex), 0);

    ex.running = true;
    TF_CHECK_INT(executor_process(&ex), -1);
    ex.running = false;
    tf_end();

    tf_begin("robot/executor 空指针保护");
    TF_CHECK_INT(executor_start(NULL, &plan), -1);
    TF_CHECK_INT(executor_start(&ex, NULL), -1);
    TF_CHECK_INT(executor_pause(NULL), -1);
    TF_CHECK_INT(executor_resume(NULL), -1);
    TF_CHECK_INT(executor_stop(NULL), -1);
    TF_CHECK(executor_get_current_action(NULL) == NULL);
    tf_end();
}
