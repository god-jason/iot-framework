/* robot/action 单元测试：动作状态机、回调返回值映射与错误路径。 */
#include "test_framework.h"
#include "action.h"

static int s_start_ret;
static int s_update_ret;
static int s_cancel_ret;
static int s_reset_ret;

static int s_start_calls;
static int s_update_calls;
static int s_cancel_calls;
static int s_reset_calls;

static int cb_start(action_t *a)
{
    (void)a;
    s_start_calls++;
    return s_start_ret;
}

static int cb_update(action_t *a)
{
    (void)a;
    s_update_calls++;
    return s_update_ret;
}

static int cb_cancel(action_t *a)
{
    (void)a;
    s_cancel_calls++;
    return s_cancel_ret;
}

static int cb_reset(action_t *a)
{
    (void)a;
    s_reset_calls++;
    return s_reset_ret;
}

static void reset_counters(void)
{
    s_start_ret = IOT_OK;
    s_update_ret = IOT_OK;
    s_cancel_ret = IOT_OK;
    s_reset_ret = IOT_OK;
    s_start_calls = 0;
    s_update_calls = 0;
    s_cancel_calls = 0;
    s_reset_calls = 0;
}

void ut_robot_action(void)
{
    action_t a;

    tf_begin("robot/action 初始化与查询");
    memset(&a, 0, sizeof(a));
    TF_CHECK_INT(action_init(&a), 0);
    TF_CHECK_INT(a.state, ACTION_IDLE);
    TF_CHECK_INT(action_get_state(&a), ACTION_IDLE);
    TF_CHECK_INT(action_init(NULL), -1);
    TF_CHECK_INT(action_get_state(NULL), ACTION_IDLE);
    tf_end();

    tf_begin("robot/action 默认回调：启动/更新自动成功");
    reset_counters();
    memset(&a, 0, sizeof(a));
    action_init(&a);
    TF_CHECK_INT(action_start(&a), 0);
    TF_CHECK_INT(a.state, ACTION_RUNNING);
    TF_CHECK_INT(action_update(&a), 0);
    TF_CHECK_INT(a.state, ACTION_SUCCESS);
    tf_end();

    tf_begin("robot/action 启动回调失败");
    reset_counters();
    memset(&a, 0, sizeof(a));
    action_init(&a);
    a.start = cb_start;
    s_start_ret = -2;
    TF_CHECK_INT(action_start(&a), -2);
    TF_CHECK_INT(s_start_calls, 1);
    TF_CHECK_INT(a.state, ACTION_FAILED);
    tf_end();

    tf_begin("robot/action 更新回调状态映射");
    reset_counters();
    memset(&a, 0, sizeof(a));
    action_init(&a);
    a.start = cb_start;
    a.update = cb_update;

    TF_CHECK_INT(action_start(&a), 0);
    s_update_ret = 0;
    TF_CHECK_INT(action_update(&a), 0);
    TF_CHECK_INT(a.state, ACTION_RUNNING);

    s_update_ret = 5;
    TF_CHECK_INT(action_update(&a), 0);
    TF_CHECK_INT(a.state, ACTION_SUCCESS);

    action_start(&a);
    s_update_ret = -1;
    TF_CHECK_INT(action_update(&a), 0);
    TF_CHECK_INT(a.state, ACTION_FAILED);
    TF_CHECK_INT(s_update_calls, 3);
    tf_end();

    tf_begin("robot/action 非 RUNNING 状态不可更新");
    reset_counters();
    memset(&a, 0, sizeof(a));
    action_init(&a);
    TF_CHECK_INT(action_update(&a), -1);
    action_cancel(&a);
    TF_CHECK_INT(action_update(&a), -1);
    tf_end();

    tf_begin("robot/action 取消");
    reset_counters();
    memset(&a, 0, sizeof(a));
    action_init(&a);
    a.cancel = cb_cancel;
    s_cancel_ret = 0;
    TF_CHECK_INT(action_cancel(&a), 0);
    TF_CHECK_INT(s_cancel_calls, 1);
    TF_CHECK_INT(a.state, ACTION_CANCELLED);

    s_cancel_ret = -3;
    TF_CHECK_INT(action_cancel(&a), -3);
    TF_CHECK_INT(a.state, ACTION_CANCELLED);
    tf_end();

    tf_begin("robot/action 复位");
    reset_counters();
    memset(&a, 0, sizeof(a));
    action_init(&a);
    a.reset = cb_reset;
    a.state = ACTION_FAILED;
    TF_CHECK_INT(action_reset(&a), 0);
    TF_CHECK_INT(s_reset_calls, 1);
    TF_CHECK_INT(a.state, ACTION_IDLE);

    a.state = ACTION_FAILED;
    s_reset_ret = -1;
    TF_CHECK_INT(action_reset(&a), -1);
    TF_CHECK_INT(a.state, ACTION_IDLE);
    tf_end();

    tf_begin("robot/action 空指针保护");
    TF_CHECK_INT(action_start(NULL), -1);
    TF_CHECK_INT(action_update(NULL), -1);
    TF_CHECK_INT(action_cancel(NULL), -1);
    TF_CHECK_INT(action_reset(NULL), -1);
    tf_end();
}
