/*
 * ros_task_test.c - 任务测试（对应 ros_task.c）
 *
 * 覆盖：创建参数校验、tick 与睡眠、同优先级轮转、优先级调度、
 *       任务退出、并行输出、任务通知、优先级动态调整。
 */

#include "test.h"
#include "ros.h"

/* ============================== 静态对象 ============================== */

static ros_task_t s_dummy;
static ros_task_t s_worker_a;
static ros_task_t s_worker_b;
static ros_task_t s_worker_high;
static ros_task_t s_worker_low;
static ros_task_t s_exit_task;
static ros_task_t s_prio_task;
static ros_task_t s_par1;
static ros_task_t s_par2;
static ros_task_t s_par3;

static uint8_t s_dummy_stack[2048];
static uint8_t s_worker_stack_a[2048];
static uint8_t s_worker_stack_b[2048];
static uint8_t s_worker_stack_h[2048];
static uint8_t s_worker_stack_l[2048];
static uint8_t s_exit_stack[2048];
static uint8_t s_prio_stack[2048];
static uint8_t s_par_stack1[3072];
static uint8_t s_par_stack2[3072];
static uint8_t s_par_stack3[3072];

static char s_order[16];
static int s_order_len;

static char s_parallel_log[16];
static int s_parallel_len;

static volatile int s_exit_ran;

/* ============================== 辅助任务 ============================== */

static void record(char c)
{
    if (s_order_len < (int)sizeof(s_order) - 1) {
        s_order[s_order_len++] = c;
    }
}

static void dummy_entry(void *arg)
{
    (void)arg;
    ros_sleep_ms(100000);
}

static void worker_a_entry(void *arg)
{
    (void)arg;
    record('A');
    ros_sleep_ms(100000);
}

static void worker_b_entry(void *arg)
{
    (void)arg;
    record('B');
    ros_sleep_ms(100000);
}

static void worker_high_entry(void *arg)
{
    (void)arg;
    record('H');
    ros_sleep_ms(100000);
}

static void worker_low_entry(void *arg)
{
    (void)arg;
    record('L');
    ros_sleep_ms(100000);
}

static void exit_entry(void *arg)
{
    (void)arg;
    s_exit_ran = 1;
}

static void prio_entry(void *arg)
{
    (void)arg;
    for (;;) {
        ros_sleep_ms(1000);
    }
}

static void parallel_entry(void *arg)
{
    int id = (int)(intptr_t)arg;
    int round;

    for (round = 1; round <= 3; round++) {
        if (s_parallel_len < (int)sizeof(s_parallel_log) - 1) {
            s_parallel_log[s_parallel_len++] = (char)('0' + id);
        }
        ros_yield();
    }
}

/* ============================== 用例 ============================== */

static void test_task_create(void)
{
    tf_begin("ros/任务创建参数校验");

    TF_CHECK(ros_current() != NULL);
    TF_CHECK_STR(ros_current()->name, "driver");

    TF_CHECK_INT(ros_task_create(NULL, "x", 1, dummy_entry, NULL,
                                 s_dummy_stack, sizeof(s_dummy_stack)),
                 ROS_INVALID_PARAM);
    TF_CHECK_INT(ros_task_create(&s_dummy, "x", 1, NULL, NULL,
                                 s_dummy_stack, sizeof(s_dummy_stack)),
                 ROS_INVALID_PARAM);
    TF_CHECK_INT(ros_task_create(&s_dummy, "x", 1, dummy_entry, NULL,
                                 NULL, sizeof(s_dummy_stack)),
                 ROS_INVALID_PARAM);
    TF_CHECK_INT(ros_task_create(&s_dummy, "x", 1, dummy_entry, NULL,
                                 s_dummy_stack, 0U),
                 ROS_INVALID_PARAM);
    TF_CHECK_INT(ros_task_create(&s_dummy, "x", ROS_PRIORITY_IDLE,
                                 dummy_entry, NULL,
                                 s_dummy_stack, sizeof(s_dummy_stack)),
                 ROS_INVALID_PARAM);
    TF_CHECK_INT(ros_task_create(&s_dummy, "dummy", 5, dummy_entry, NULL,
                                 s_dummy_stack, sizeof(s_dummy_stack)),
                 ROS_OK);
    TF_CHECK(s_dummy.created);
    TF_CHECK_INT(s_dummy.priority, 5);

    tf_end();
}

static void test_tick_sleep(void)
{
    uint32_t t0;

    tf_begin("ros/tick 与睡眠");

    t0 = ros_tick_get();
    ros_tick_increment(10U);
    TF_CHECK_INT((int)(ros_tick_get() - t0), 10);

    ros_tick();
    TF_CHECK_INT((int)(ros_tick_get() - t0), 11);

    t0 = ros_tick_get();
    ros_sleep_ms(5U);
    TF_CHECK((ros_tick_get() - t0) >= 5U);

    t0 = ros_tick_get();
    ros_sleep_until(t0 + 3U);
    TF_CHECK((ros_tick_get() - t0) >= 3U);

    /* 已到期时刻不应睡眠 */
    t0 = ros_tick_get();
    ros_sleep_until(t0);
    TF_CHECK_INT((int)(ros_tick_get() - t0), 0);

    tf_end();
}

static void test_round_robin(void)
{
    tf_begin("ros/同优先级轮转");

    s_order_len = 0;
    TF_CHECK_INT(ros_task_create(&s_worker_a, "a", 5, worker_a_entry, NULL,
                                 s_worker_stack_a, sizeof(s_worker_stack_a)),
                 ROS_OK);
    TF_CHECK_INT(ros_task_create(&s_worker_b, "b", 5, worker_b_entry, NULL,
                                 s_worker_stack_b, sizeof(s_worker_stack_b)),
                 ROS_OK);

    ros_sleep_ms(1U);
    s_order[s_order_len] = '\0';
    TF_CHECK_STR(s_order, "AB");

    tf_end();
}

static void test_priority(void)
{
    tf_begin("ros/优先级调度");

    s_order_len = 0;
    TF_CHECK_INT(ros_task_create(&s_worker_high, "high", 1,
                                 worker_high_entry, NULL,
                                 s_worker_stack_h, sizeof(s_worker_stack_h)),
                 ROS_OK);
    TF_CHECK_INT(ros_task_create(&s_worker_low, "low", 5,
                                 worker_low_entry, NULL,
                                 s_worker_stack_l, sizeof(s_worker_stack_l)),
                 ROS_OK);

    ros_sleep_ms(1U);
    s_order[s_order_len] = '\0';
    TF_CHECK_STR(s_order, "HL");

    tf_end();
}

static void test_task_exit(void)
{
    tf_begin("ros/任务退出");

    s_exit_ran = 0;
    TF_CHECK_INT(ros_task_create(&s_exit_task, "exit", 5, exit_entry, NULL,
                                 s_exit_stack, sizeof(s_exit_stack)),
                 ROS_OK);
    ros_sleep_ms(1U);
    TF_CHECK_INT(s_exit_ran, 1);
    TF_CHECK_INT(s_exit_task.state, ROS_TASK_TERMINATED);

    tf_end();
}

static void test_parallel_output(void)
{
    tf_begin("ros/多任务并行输出");

    s_parallel_len = 0;
    memset(s_parallel_log, 0, sizeof(s_parallel_log));

    TF_CHECK_INT(ros_task_create(&s_par1, "P1", 4, parallel_entry,
                                 (void *)(intptr_t)1,
                                 s_par_stack1, sizeof(s_par_stack1)), ROS_OK);
    TF_CHECK_INT(ros_task_create(&s_par2, "P2", 4, parallel_entry,
                                 (void *)(intptr_t)2,
                                 s_par_stack2, sizeof(s_par_stack2)), ROS_OK);
    TF_CHECK_INT(ros_task_create(&s_par3, "P3", 4, parallel_entry,
                                 (void *)(intptr_t)3,
                                 s_par_stack3, sizeof(s_par_stack3)), ROS_OK);

    ros_sleep_ms(1U);

    s_parallel_log[s_parallel_len] = '\0';
    TF_CHECK_STR(s_parallel_log, "123123123");

    tf_end();
}

static void test_notify(void)
{
    ros_task_t *self = ros_current();
    uint32_t value = 0U;

    tf_begin("ros/任务通知");

    TF_CHECK_INT(ros_task_notify(NULL, 1U), ROS_INVALID_PARAM);

    TF_CHECK_INT(ros_task_notify(self, 0x5A5AU), ROS_OK);
    TF_CHECK_INT(ros_task_wait_notify(self, ROS_TIMEOUT_NONE, &value), ROS_OK);
    TF_CHECK_INT(value, 0x5A5A);

    /* 已消费：再次等待应超时 */
    TF_CHECK_INT(ros_task_wait_notify(self, ROS_TIMEOUT_NONE, &value), ROS_TIMEOUT);

#if ROS_USE_NOTIFY
    TF_CHECK_INT(ros_task_notify(self, 0x11U), ROS_OK);
    ros_task_notify_clear(self);
    TF_CHECK_INT(ros_task_wait_notify(self, ROS_TIMEOUT_NONE, &value), ROS_TIMEOUT);
#endif

    tf_end();
}

static void test_priority_set(void)
{
    tf_begin("ros/优先级动态调整");

    TF_CHECK_INT(ros_task_create(&s_prio_task, "prio", 5U, prio_entry, NULL,
                                 s_prio_stack, sizeof(s_prio_stack)), ROS_OK);

    TF_CHECK_INT(ros_task_set_priority(&s_prio_task, 6U), ROS_OK);
    TF_CHECK_INT(s_prio_task.priority, 6);
    TF_CHECK_INT(s_prio_task.base_priority, 6);

    TF_CHECK_INT(ros_task_set_priority(&s_prio_task, ROS_PRIORITY_IDLE),
                 ROS_INVALID_PARAM);
    TF_CHECK_INT(ros_task_set_priority(NULL, 3U), ROS_INVALID_PARAM);

    TF_CHECK_INT(ros_task_set_priority(&s_prio_task, 5U), ROS_OK);
    TF_CHECK_INT(s_prio_task.priority, 5);

    tf_end();
}

/* ============================== 入口 ============================== */

void ros_test_task(void)
{
    test_task_create();
    test_tick_sleep();
    test_round_robin();
    test_priority();
    test_task_exit();
    test_parallel_output();
    test_notify();
    test_priority_set();
}
