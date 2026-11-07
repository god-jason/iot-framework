/* src/iot_robot_fsm.c 单元测试：状态机初始化、启动/停止、事件派发与转换。 */
#include "test_framework.h"
#include "iot_robot_fsm.h"

#include <string.h>

typedef struct {
    int entry;
    int action;
    int exit;
} state_log_t;

static state_log_t s_l1;
static state_log_t s_l2;
static state_log_t s_l3;

static int s_guard_calls;
static bool s_guard_allow;
static uint32_t s_last_event;
static void *s_last_data;

static void on_entry(iot_robot_fsm_t *fsm, void *ctx)
{
    (void)fsm;
    if (ctx != NULL) {
        ((state_log_t *)ctx)->entry++;
    }
}

static void on_action(iot_robot_fsm_t *fsm, void *ctx, uint32_t event, void *data)
{
    (void)fsm;
    if (ctx != NULL) {
        ((state_log_t *)ctx)->action++;
    }
    s_last_event = event;
    s_last_data = data;
}

static void on_exit(iot_robot_fsm_t *fsm, void *ctx)
{
    (void)fsm;
    if (ctx != NULL) {
        ((state_log_t *)ctx)->exit++;
    }
}

static bool guard_fn(iot_robot_fsm_t *fsm, void *ctx, uint32_t event, void *data)
{
    (void)fsm;
    (void)ctx;
    (void)event;
    (void)data;
    s_guard_calls++;
    return s_guard_allow;
}

static const iot_robot_fsm_state_t s_states[3] = {
    { 1, on_entry, on_action, on_exit, &s_l1 },
    { 2, on_entry, on_action, on_exit, &s_l2 },
    { 3, on_entry, on_action, on_exit, &s_l3 }
};

/* 1 --10--> 2 --11(guard)--> 3 --10--> 1；2 --12--> 非法目标 99 */
static const iot_robot_fsm_transition_t s_transitions[4] = {
    { 1, 10, 2, NULL },
    { 2, 11, 3, guard_fn },
    { 2, 12, 99, NULL },
    { 3, 10, 1, NULL }
};

static void reset_fixture(void)
{
    memset(&s_l1, 0, sizeof(s_l1));
    memset(&s_l2, 0, sizeof(s_l2));
    memset(&s_l3, 0, sizeof(s_l3));
    s_guard_calls = 0;
    s_guard_allow = true;
    s_last_event = 0;
    s_last_data = NULL;
}

void ut_iot_robot_fsm(void)
{
    iot_robot_fsm_t fsm;

    tf_begin("fsm/init 校验");
    TF_CHECK_INT(iot_robot_fsm_init(NULL, s_states, 3, s_transitions, 4, NULL), -1);
    TF_CHECK_INT(iot_robot_fsm_init(&fsm, NULL, 3, s_transitions, 4, NULL), -1);
    TF_CHECK_INT(iot_robot_fsm_init(&fsm, s_states, 0, s_transitions, 4, NULL), -1);

    TF_CHECK_INT(iot_robot_fsm_init(&fsm, s_states, 3, s_transitions, 4, &fsm), IOT_OK);
    TF_CHECK_INT(iot_robot_fsm_get_state(&fsm), 0);
    TF_CHECK_INT(iot_robot_fsm_is_running(&fsm), 0);
    TF_CHECK_INT(iot_robot_fsm_is_state(&fsm, 1), 0);
    /* 无转换表也能初始化 */
    TF_CHECK_INT(iot_robot_fsm_init(&fsm, s_states, 3, NULL, 0, NULL), IOT_OK);
    tf_end();

    tf_begin("fsm/start 与 stop");
    reset_fixture();
    TF_CHECK_INT(iot_robot_fsm_init(&fsm, s_states, 3, s_transitions, 4, NULL), IOT_OK);
    TF_CHECK_INT(iot_robot_fsm_start(&fsm, 99), -1);
    TF_CHECK_INT(iot_robot_fsm_is_running(&fsm), 0);

    TF_CHECK_INT(iot_robot_fsm_start(&fsm, 1), IOT_OK);
    TF_CHECK_INT(iot_robot_fsm_is_running(&fsm), 1);
    TF_CHECK_INT(iot_robot_fsm_get_state(&fsm), 1);
    TF_CHECK_INT(iot_robot_fsm_is_state(&fsm, 1), 1);
    TF_CHECK_INT(s_l1.entry, 1);

    TF_CHECK_INT(iot_robot_fsm_stop(&fsm), IOT_OK);
    TF_CHECK_INT(iot_robot_fsm_is_running(&fsm), 0);
    TF_CHECK_INT(s_l1.exit, 1);
    /* 已停止时再次 stop 不再触发 exit */
    TF_CHECK_INT(iot_robot_fsm_stop(&fsm), IOT_OK);
    TF_CHECK_INT(s_l1.exit, 1);
    /* current 保留，状态查询仍有效 */
    TF_CHECK_INT(iot_robot_fsm_get_state(&fsm), 1);

    TF_CHECK_INT(iot_robot_fsm_stop(NULL), -1);
    TF_CHECK_INT(iot_robot_fsm_get_state(NULL), 0);
    TF_CHECK_INT(iot_robot_fsm_is_running(NULL), 0);
    TF_CHECK_INT(iot_robot_fsm_is_state(NULL, 1), 0);
    tf_end();

    tf_begin("fsm/dispatch 正常转换");
    reset_fixture();
    TF_CHECK_INT(iot_robot_fsm_init(&fsm, s_states, 3, s_transitions, 4, NULL), IOT_OK);
    TF_CHECK_INT(iot_robot_fsm_start(&fsm, 1), IOT_OK);

    TF_CHECK_INT(iot_robot_fsm_dispatch(&fsm, 10, NULL), IOT_OK);
    TF_CHECK_INT(s_l1.action, 1);
    TF_CHECK_INT(s_l1.exit, 1);
    TF_CHECK_INT(s_l2.entry, 1);
    TF_CHECK_INT(iot_robot_fsm_get_state(&fsm), 2);
    TF_CHECK_INT(s_last_event, 10);

    {
        int payload = 123;
        TF_CHECK_INT(iot_robot_fsm_dispatch(&fsm, 11, &payload), IOT_OK);
        TF_CHECK(s_last_data == &payload);
    }
    TF_CHECK_INT(s_guard_calls, 1);
    TF_CHECK_INT(s_l2.exit, 1);
    TF_CHECK_INT(s_l3.entry, 1);
    TF_CHECK_INT(iot_robot_fsm_get_state(&fsm), 3);

    /* 3 --10--> 1 */
    TF_CHECK_INT(iot_robot_fsm_dispatch(&fsm, 10, NULL), IOT_OK);
    TF_CHECK_INT(iot_robot_fsm_get_state(&fsm), 1);
    TF_CHECK_INT(s_l3.exit, 1);
    TF_CHECK_INT(s_l1.entry, 2);
    tf_end();

    tf_begin("fsm/guard 拒绝");
    reset_fixture();
    TF_CHECK_INT(iot_robot_fsm_init(&fsm, s_states, 3, s_transitions, 4, NULL), IOT_OK);
    TF_CHECK_INT(iot_robot_fsm_start(&fsm, 2), IOT_OK);
    s_guard_allow = false;

    TF_CHECK_INT(iot_robot_fsm_dispatch(&fsm, 11, NULL), IOT_OK);
    TF_CHECK_INT(s_guard_calls, 1);
    TF_CHECK_INT(s_l2.action, 1);
    TF_CHECK_INT(s_l2.exit, 0);
    TF_CHECK_INT(s_l3.entry, 0);
    TF_CHECK_INT(iot_robot_fsm_get_state(&fsm), 2);
    tf_end();

    tf_begin("fsm/无匹配转换与非法目标");
    reset_fixture();
    TF_CHECK_INT(iot_robot_fsm_init(&fsm, s_states, 3, s_transitions, 4, NULL), IOT_OK);
    TF_CHECK_INT(iot_robot_fsm_start(&fsm, 1), IOT_OK);

    /* 无匹配事件：仅执行 action，状态不变 */
    TF_CHECK_INT(iot_robot_fsm_dispatch(&fsm, 77, NULL), IOT_OK);
    TF_CHECK_INT(s_l1.action, 1);
    TF_CHECK_INT(iot_robot_fsm_get_state(&fsm), 1);

    /* 转换目标不存在：返回 -1，且不触发 exit */
    TF_CHECK_INT(iot_robot_fsm_start(&fsm, 2), IOT_OK);
    TF_CHECK_INT(iot_robot_fsm_dispatch(&fsm, 12, NULL), -1);
    TF_CHECK_INT(s_l2.exit, 0);
    TF_CHECK_INT(iot_robot_fsm_get_state(&fsm), 2);
    tf_end();

    tf_begin("fsm/pause 与 resume");
    reset_fixture();
    TF_CHECK_INT(iot_robot_fsm_init(&fsm, s_states, 3, s_transitions, 4, NULL), IOT_OK);
    TF_CHECK_INT(iot_robot_fsm_start(&fsm, 1), IOT_OK);

    TF_CHECK_INT(iot_robot_fsm_pause(NULL), -1);
    TF_CHECK_INT(iot_robot_fsm_resume(NULL), -1);
    TF_CHECK_INT(iot_robot_fsm_pause(&fsm), IOT_OK);
    TF_CHECK_INT(iot_robot_fsm_dispatch(&fsm, 10, NULL), -1);
    TF_CHECK_INT(s_l1.action, 0);
    TF_CHECK_INT(iot_robot_fsm_get_state(&fsm), 1);

    TF_CHECK_INT(iot_robot_fsm_resume(&fsm), IOT_OK);
    TF_CHECK_INT(iot_robot_fsm_dispatch(&fsm, 10, NULL), IOT_OK);
    TF_CHECK_INT(iot_robot_fsm_get_state(&fsm), 2);
    tf_end();

    tf_begin("fsm/未启动时派发 / 停止后派发");
    reset_fixture();
    TF_CHECK_INT(iot_robot_fsm_init(&fsm, s_states, 3, s_transitions, 4, NULL), IOT_OK);
    TF_CHECK_INT(iot_robot_fsm_dispatch(&fsm, 10, NULL), -1);
    TF_CHECK_INT(iot_robot_fsm_transition(&fsm, 2), -1);

    TF_CHECK_INT(iot_robot_fsm_start(&fsm, 1), IOT_OK);
    TF_CHECK_INT(iot_robot_fsm_stop(&fsm), IOT_OK);
    TF_CHECK_INT(iot_robot_fsm_dispatch(&fsm, 10, NULL), -1);
    TF_CHECK_INT(s_l1.action, 0);
    tf_end();

    tf_begin("fsm/直接转换");
    reset_fixture();
    TF_CHECK_INT(iot_robot_fsm_init(&fsm, s_states, 3, s_transitions, 4, NULL), IOT_OK);
    TF_CHECK_INT(iot_robot_fsm_start(&fsm, 1), IOT_OK);

    TF_CHECK_INT(iot_robot_fsm_transition(NULL, 3), -1);
    TF_CHECK_INT(iot_robot_fsm_transition(&fsm, 3), IOT_OK);
    TF_CHECK_INT(s_l1.exit, 1);
    TF_CHECK_INT(s_l3.entry, 1);
    TF_CHECK_INT(iot_robot_fsm_get_state(&fsm), 3);

    TF_CHECK_INT(iot_robot_fsm_transition(&fsm, 99), -1);
    TF_CHECK_INT(iot_robot_fsm_get_state(&fsm), 3);
    TF_CHECK_INT(s_l3.exit, 0);
    tf_end();
}
