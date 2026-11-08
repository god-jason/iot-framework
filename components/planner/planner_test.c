/* robot/planner 单元测试：计划注册/移除、容量上限、启动停止与单步执行。 */
#include "test.h"
#include "planner.h"

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

static void setup_plan(planner_plan_t *p, uint32_t id,
                       action_t **acts, uint16_t count)
{
    memset(p, 0, sizeof(*p));
    p->id = id;
    p->actions = acts;
    p->action_count = count;
}

static action_t s_cap_action;
static action_t *s_cap_acts[1];
static planner_plan_t s_cap_plans[9];

void ut_robot_planner(void)
{
    planner_plan_t plan;
    action_t a1;
    action_t *acts[1];
    int i;

    tf_begin("robot/planner 初始化");
    TF_CHECK_INT(planner_init(), 0);
    TF_CHECK(planner_get_current() == NULL);
    tf_end();

    tf_begin("robot/planner 非法计划");
    TF_CHECK_INT(planner_add(NULL), -1);
    setup_plan(&plan, 1, NULL, 1);
    TF_CHECK_INT(planner_add(&plan), -1);
    setup_action(&a1, cb_success);
    acts[0] = &a1;
    setup_plan(&plan, 1, acts, 0);
    TF_CHECK_INT(planner_add(&plan), -1);
    tf_end();

    tf_begin("robot/planner 添加计划");
    planner_init();
    setup_action(&a1, cb_success);
    acts[0] = &a1;
    setup_plan(&plan, 11, acts, 1);
    TF_CHECK_INT(planner_add(&plan), 0);
    TF_CHECK_INT(plan.state, PLANNER_PLAN_PENDING);
    TF_CHECK_INT(plan.current_action, 0);
    TF_CHECK(planner_get_current() == NULL);
    tf_end();

    tf_begin("robot/planner 容量上限");
    planner_init();
    setup_action(&s_cap_action, cb_success);
    s_cap_acts[0] = &s_cap_action;
    for (i = 0; i < 9; i++) {
        setup_plan(&s_cap_plans[i], (uint32_t)(i + 1), s_cap_acts, 1);
    }
    for (i = 0; i < 8; i++) {
        TF_CHECK_INT(planner_add(&s_cap_plans[i]), 0);
    }
    TF_CHECK_INT(planner_add(&s_cap_plans[8]), -1);
    /* 移除一个后应可再添加 */
    TF_CHECK_INT(planner_remove(1), 0);
    TF_CHECK_INT(planner_add(&s_cap_plans[8]), 0);
    tf_end();

    tf_begin("robot/planner 移除计划");
    planner_init();
    setup_action(&a1, cb_success);
    acts[0] = &a1;
    setup_plan(&plan, 21, acts, 1);
    TF_CHECK_INT(planner_add(&plan), 0);
    TF_CHECK_INT(planner_remove(21), 0);
    TF_CHECK_INT(planner_remove(21), -1);
    TF_CHECK_INT(planner_remove(999), -1);
    tf_end();

    tf_begin("robot/planner 启动与停止");
    planner_init();
    setup_action(&a1, cb_success);
    acts[0] = &a1;
    setup_plan(&plan, 31, acts, 1);
    TF_CHECK_INT(planner_add(&plan), 0);

    TF_CHECK_INT(planner_start(999), -1);
    TF_CHECK_INT(planner_start(31), 0);
    TF_CHECK_INT(plan.state, PLANNER_PLAN_RUNNING);
    TF_CHECK_INT(plan.current_action, 0);
    TF_CHECK(planner_get_current() == &plan);

    TF_CHECK_INT(planner_stop(999), -1);
    TF_CHECK_INT(planner_stop(31), 0);
    TF_CHECK_INT(plan.state, PLANNER_PLAN_PENDING);
    TF_CHECK(planner_get_current() == NULL);
    tf_end();

    tf_begin("robot/planner 执行成功");
    planner_init();
    setup_action(&a1, cb_success);
    acts[0] = &a1;
    setup_plan(&plan, 41, acts, 1);
    planner_add(&plan);
    planner_start(41);
    TF_CHECK_INT(planner_process(), 0);
    TF_CHECK_INT(plan.state, PLANNER_PLAN_SUCCESS);
    TF_CHECK_INT(plan.current_action, 1);
    TF_CHECK(planner_get_current() == NULL);
    /* 无当前计划时再次推进应安全返回 */
    TF_CHECK_INT(planner_process(), 0);
    tf_end();

    tf_begin("robot/planner 分拍执行");
    planner_init();
    s_once_calls = 0;
    setup_action(&a1, cb_run_once);
    acts[0] = &a1;
    setup_plan(&plan, 51, acts, 1);
    planner_add(&plan);
    planner_start(51);
    TF_CHECK_INT(planner_process(), 0);
    TF_CHECK_INT(plan.state, PLANNER_PLAN_RUNNING);
    TF_CHECK_INT(plan.current_action, 0);
    TF_CHECK(planner_get_current() == &plan);
    TF_CHECK_INT(planner_process(), 0);
    TF_CHECK_INT(plan.state, PLANNER_PLAN_SUCCESS);
    TF_CHECK(planner_get_current() == NULL);
    tf_end();

    tf_begin("robot/planner 执行失败");
    planner_init();
    setup_action(&a1, cb_failure);
    acts[0] = &a1;
    setup_plan(&plan, 61, acts, 1);
    planner_add(&plan);
    planner_start(61);
    TF_CHECK_INT(planner_process(), 0);
    TF_CHECK_INT(plan.state, PLANNER_PLAN_FAILED);
    TF_CHECK_INT(plan.current_action, 0);
    TF_CHECK(planner_get_current() == NULL);
    tf_end();

    tf_begin("robot/planner 停止后不推进");
    planner_init();
    setup_action(&a1, cb_success);
    acts[0] = &a1;
    setup_plan(&plan, 71, acts, 1);
    planner_add(&plan);
    planner_start(71);
    planner_stop(71);
    TF_CHECK_INT(planner_process(), 0);
    TF_CHECK_INT(plan.state, PLANNER_PLAN_PENDING);
    TF_CHECK_INT(plan.current_action, 0);
    tf_end();

    tf_begin("robot/planner 移除当前计划");
    planner_init();
    setup_action(&a1, cb_success);
    acts[0] = &a1;
    setup_plan(&plan, 81, acts, 1);
    planner_add(&plan);
    planner_start(81);
    TF_CHECK(planner_get_current() == &plan);
    TF_CHECK_INT(planner_remove(81), 0);
    TF_CHECK(planner_get_current() == NULL);
    tf_end();
}
