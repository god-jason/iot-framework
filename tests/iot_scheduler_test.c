/* core/iot_scheduler.c 单元测试：任务注册、周期调度、状态与错误统计。 */
#include "test_framework.h"
#include "iot_scheduler.h"

#include <string.h>

static int s_cb_calls;
static int s_fail_calls;
static void *s_cb_user;

static int task_cb(void *user)
{
    s_cb_calls++;
    s_cb_user = user;
    return IOT_OK;
}

static int task_fail(void *user)
{
    (void)user;
    s_fail_calls++;
    return -1;
}

static iot_task_t s_pool[17];
static char s_pool_names[17][8];

void ut_iot_scheduler(void)
{
    iot_task_t t;
    iot_task_t t2;
    iot_task_t bad;
    int i;

    tf_begin("scheduler/init 与 add 校验");
    TF_CHECK_INT(iot_scheduler_init(), IOT_OK);
    TF_CHECK_INT(iot_scheduler_add(NULL), -1);
    memset(&bad, 0, sizeof(bad));
    bad.name = "bad";
    bad.period = 10;
    TF_CHECK_INT(iot_scheduler_add(&bad), -1);

    memset(&t, 0, sizeof(t));
    t.name = "task";
    t.callback = task_cb;
    t.period = 3;
    TF_CHECK_INT(iot_scheduler_add(&t), IOT_OK);
    TF_CHECK_INT(t.state, IOT_TASK_READY);
    TF_CHECK_INT(t.next_run, 0);
    TF_CHECK_INT(t.run_count, 0);
    TF_CHECK_INT(t.error_count, 0);
    tf_end();

    tf_begin("scheduler/周期任务");
    TF_CHECK_INT(iot_scheduler_init(), IOT_OK);
    s_cb_calls = 0;
    s_cb_user = NULL;
    memset(&t, 0, sizeof(t));
    t.name = "periodic";
    t.callback = task_cb;
    t.period = 3;
    t.user = &t;
    TF_CHECK_INT(iot_scheduler_add(&t), IOT_OK);

    TF_CHECK_INT(iot_scheduler_process(), IOT_OK);
    TF_CHECK_INT(s_cb_calls, 1);
    TF_CHECK_INT(s_cb_user == &t, 1);
    TF_CHECK_INT(t.run_count, 1);
    TF_CHECK_INT(t.next_run, 4);

    TF_CHECK_INT(iot_scheduler_process(), IOT_OK); /* tick 2 */
    TF_CHECK_INT(iot_scheduler_process(), IOT_OK); /* tick 3 */
    TF_CHECK_INT(s_cb_calls, 1);

    TF_CHECK_INT(iot_scheduler_process(), IOT_OK); /* tick 4 */
    TF_CHECK_INT(s_cb_calls, 2);
    TF_CHECK_INT(t.run_count, 2);
    TF_CHECK_INT(t.next_run, 7);
    tf_end();

    tf_begin("scheduler/一次性任务");
    TF_CHECK_INT(iot_scheduler_init(), IOT_OK);
    s_cb_calls = 0;
    memset(&t, 0, sizeof(t));
    t.name = "oneshot";
    t.callback = task_cb;
    t.period = 0;
    TF_CHECK_INT(iot_scheduler_add(&t), IOT_OK);

    TF_CHECK_INT(iot_scheduler_process(), IOT_OK);
    TF_CHECK_INT(t.run_count, 1);
    TF_CHECK_INT(t.state, IOT_TASK_SLEEPING);
    TF_CHECK_INT(t.next_run, 0);
    /* 当前实现中 SLEEPING 不会被跳过，下一次 process 仍会执行 */
    TF_CHECK_INT(iot_scheduler_process(), IOT_OK);
    TF_CHECK_INT(t.run_count, 2);
    tf_end();

    tf_begin("scheduler/disable 与 enable");
    TF_CHECK_INT(iot_scheduler_init(), IOT_OK);
    s_cb_calls = 0;
    memset(&t, 0, sizeof(t));
    t.name = "togglable";
    t.callback = task_cb;
    t.period = 1;
    TF_CHECK_INT(iot_scheduler_add(&t), IOT_OK);
    TF_CHECK_INT(iot_scheduler_process(), IOT_OK);
    TF_CHECK_INT(t.run_count, 1);

    TF_CHECK_INT(iot_scheduler_disable(&t), IOT_OK);
    TF_CHECK_INT(t.state, IOT_TASK_DISABLED);
    TF_CHECK_INT(iot_scheduler_process(), IOT_OK);
    TF_CHECK_INT(t.run_count, 1);

    TF_CHECK_INT(iot_scheduler_enable(NULL), -1);
    TF_CHECK_INT(iot_scheduler_enable(&t), IOT_OK);
    TF_CHECK_INT(t.state, IOT_TASK_READY);
    TF_CHECK_INT(iot_scheduler_process(), IOT_OK);
    TF_CHECK_INT(t.run_count, 2);

    TF_CHECK_INT(iot_scheduler_disable(NULL), -1);
    tf_end();

    tf_begin("scheduler/回调错误统计");
    TF_CHECK_INT(iot_scheduler_init(), IOT_OK);
    s_fail_calls = 0;
    memset(&t, 0, sizeof(t));
    t.name = "fail";
    t.callback = task_fail;
    t.period = 2;
    TF_CHECK_INT(iot_scheduler_add(&t), IOT_OK);

    TF_CHECK_INT(iot_scheduler_process(), IOT_OK);
    TF_CHECK_INT(s_fail_calls, 1);
    TF_CHECK_INT(t.error_count, 1);
    TF_CHECK_INT(t.state, IOT_TASK_ERROR);

    /* ERROR 状态不会被跳过，下一周期继续执行 */
    TF_CHECK_INT(iot_scheduler_process(), IOT_OK); /* tick 2 */
    TF_CHECK_INT(s_fail_calls, 1);
    TF_CHECK_INT(iot_scheduler_process(), IOT_OK); /* tick 3 */
    TF_CHECK_INT(s_fail_calls, 2);
    TF_CHECK_INT(t.error_count, 2);
    tf_end();

    tf_begin("scheduler/find 与 remove");
    TF_CHECK_INT(iot_scheduler_init(), IOT_OK);
    memset(&t, 0, sizeof(t));
    t.name = "alpha";
    t.callback = task_cb;
    t.period = 5;
    memset(&t2, 0, sizeof(t2));
    t2.name = "beta";
    t2.callback = task_cb;
    t2.period = 5;
    TF_CHECK_INT(iot_scheduler_add(&t), IOT_OK);
    TF_CHECK_INT(iot_scheduler_add(&t2), IOT_OK);

    TF_CHECK(iot_scheduler_find("alpha") == &t);
    TF_CHECK(iot_scheduler_find("beta") == &t2);
    TF_CHECK(iot_scheduler_find("missing") == NULL);
    TF_CHECK(iot_scheduler_find(NULL) == NULL);

    TF_CHECK_INT(iot_scheduler_remove(NULL), -1);
    TF_CHECK_INT(iot_scheduler_remove(&t), IOT_OK);
    TF_CHECK_INT(t.state, IOT_TASK_DISABLED);
    TF_CHECK(iot_scheduler_find("alpha") == NULL);
    TF_CHECK_INT(iot_scheduler_remove(&t), -1);

    TF_CHECK_INT(iot_scheduler_clear(), IOT_OK);
    TF_CHECK(iot_scheduler_find("beta") == NULL);
    tf_end();

    tf_begin("scheduler/容量上限");
    TF_CHECK_INT(iot_scheduler_init(), IOT_OK);
    for (i = 0; i < 17; i++) {
        memset(&s_pool[i], 0, sizeof(s_pool[i]));
        snprintf(s_pool_names[i], sizeof(s_pool_names[i]), "t%d", i);
        s_pool[i].name = s_pool_names[i];
        s_pool[i].callback = task_cb;
        s_pool[i].period = 10;
    }
    for (i = 0; i < 16; i++) {
        TF_CHECK_INT(iot_scheduler_add(&s_pool[i]), IOT_OK);
    }
    TF_CHECK_INT(iot_scheduler_add(&s_pool[16]), -1);
    tf_end();
}
