/*
 * iot_test_task.c - 任务相关测试
 *
 * 覆盖：任务创建参数校验、tick 推进与睡眠、同优先级轮转、
 *       优先级调度、任务入口返回后自动退出、多任务并行输出。
 */

#include "test.h"
#include "iot.h"

#include <string.h>

/* ============================== 静态对象 ============================== */

static iot_task_t s_dummy;
static iot_task_t s_worker_a;
static iot_task_t s_worker_b;
static iot_task_t s_worker_high;
static iot_task_t s_worker_low;
static iot_task_t s_exit_task;
static iot_task_t s_par1;
static iot_task_t s_par2;
static iot_task_t s_par3;

static uint8_t s_dummy_stack[2048];
static uint8_t s_worker_stack_a[2048];
static uint8_t s_worker_stack_b[2048];
static uint8_t s_worker_stack_h[2048];
static uint8_t s_worker_stack_l[2048];
static uint8_t s_exit_stack[2048];
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

/* 长期睡眠，占位但不再干扰后续测试 */
static void dummy_entry(void *arg)
{
    (void)arg;
    iot_sleep_ms(100000);
}

static void worker_a_entry(void *arg)
{
    (void)arg;
    record('A');
    iot_sleep_ms(100000);
}

static void worker_b_entry(void *arg)
{
    (void)arg;
    record('B');
    iot_sleep_ms(100000);
}

static void worker_high_entry(void *arg)
{
    (void)arg;
    record('H');
    iot_sleep_ms(100000);
}

static void worker_low_entry(void *arg)
{
    (void)arg;
    record('L');
    iot_sleep_ms(100000);
}

/* 任务入口返回即退出 */
static void exit_entry(void *arg)
{
    (void)arg;
    s_exit_ran = 1;
}

/* 并行输出任务：打印并记录自己的执行轮次，然后让出 CPU */
static void parallel_entry(void *arg)
{
    int id = (int)(intptr_t)arg;
    int round;

    for (round = 1; round <= 3; round++) {
        printf("[vtask] P%d round %d\n", id, round);
        if (s_parallel_len < (int)sizeof(s_parallel_log) - 1) {
            s_parallel_log[s_parallel_len++] = (char)('0' + id);
        }
        iot_yield();       /* 让同优先级任务交替运行 */
    }
}

/* ============================== 单项测试 ============================== */

/* 1. 任务创建参数与当前任务 */
static void test_task_create(void)
{
    tf_begin("iot/任务创建与当前任务");

    TF_CHECK(iot_current() != NULL);
    TF_CHECK_STR(iot_current()->name, "driver");

    TF_CHECK_INT(iot_task_create(NULL, "x", 1, dummy_entry, NULL,
                                  s_dummy_stack, sizeof(s_dummy_stack)),
                 IOT_INVALID_PARAM);
    TF_CHECK_INT(iot_task_create(&s_dummy, "x", 1, NULL, NULL,
                                  s_dummy_stack, sizeof(s_dummy_stack)),
                 IOT_INVALID_PARAM);
    TF_CHECK_INT(iot_task_create(&s_dummy, "x", 1, dummy_entry, NULL,
                                  NULL, sizeof(s_dummy_stack)),
                 IOT_INVALID_PARAM);
    TF_CHECK_INT(iot_task_create(&s_dummy, "x", 1, dummy_entry, NULL,
                                  s_dummy_stack, 0U),
                 IOT_INVALID_PARAM);
    TF_CHECK_INT(iot_task_create(&s_dummy, "x", IOT_PRIORITY_IDLE,
                                  dummy_entry, NULL,
                                  s_dummy_stack, sizeof(s_dummy_stack)),
                 IOT_INVALID_PARAM);

    TF_CHECK_INT(iot_task_create(&s_dummy, "dummy", 5, dummy_entry, NULL,
                                  s_dummy_stack, sizeof(s_dummy_stack)),
                 IOT_OK);
    tf_end();
}

/* 2. tick 推进与睡眠 */
static void test_tick_sleep(void)
{
    uint32_t t0;

    tf_begin("iot/tick与睡眠");

    t0 = iot_tick_get();
    iot_tick_increment(10U);
    TF_CHECK_INT((int)(iot_tick_get() - t0), 10);

    iot_tick();
    TF_CHECK_INT((int)(iot_tick_get() - t0), 11);

    t0 = iot_tick_get();
    iot_sleep_ms(5U);
    TF_CHECK((iot_tick_get() - t0) >= 5U);

    t0 = iot_tick_get();
    iot_sleep_until(t0 + 3U);
    TF_CHECK((iot_tick_get() - t0) >= 3U);

    tf_end();
}

/* 3. 同优先级轮转 */
static void test_round_robin(void)
{
    tf_begin("iot/同优先级轮转");

    s_order_len = 0;
    TF_CHECK_INT(iot_task_create(&s_worker_a, "a", 5, worker_a_entry, NULL,
                                  s_worker_stack_a, sizeof(s_worker_stack_a)),
                 IOT_OK);
    TF_CHECK_INT(iot_task_create(&s_worker_b, "b", 5, worker_b_entry, NULL,
                                  s_worker_stack_b, sizeof(s_worker_stack_b)),
                 IOT_OK);

    iot_sleep_ms(1U);              /* 让出，让同优先级任务依次运行 */
    s_order[s_order_len] = '\0';
    TF_CHECK_STR(s_order, "AB");

    tf_end();
}

/* 4. 优先级调度（值小者先运行） */
static void test_priority(void)
{
    tf_begin("iot/优先级调度");

    s_order_len = 0;
    TF_CHECK_INT(iot_task_create(&s_worker_high, "high", 1,
                                  worker_high_entry, NULL,
                                  s_worker_stack_h, sizeof(s_worker_stack_h)),
                 IOT_OK);
    TF_CHECK_INT(iot_task_create(&s_worker_low, "low", 5,
                                  worker_low_entry, NULL,
                                  s_worker_stack_l, sizeof(s_worker_stack_l)),
                 IOT_OK);

    iot_sleep_ms(1U);
    s_order[s_order_len] = '\0';
    TF_CHECK_STR(s_order, "HL");

    tf_end();
}

/* 5. 任务入口返回后自动退出 */
static void test_task_exit(void)
{
    tf_begin("iot/任务退出");

    s_exit_ran = 0;
    TF_CHECK_INT(iot_task_create(&s_exit_task, "exit", 5, exit_entry, NULL,
                                  s_exit_stack, sizeof(s_exit_stack)),
                 IOT_OK);
    iot_sleep_ms(1U);
    TF_CHECK_INT(s_exit_ran, 1);

    tf_end();
}

/* 6. 多任务并行输出（同优先级交替打印） */
static void test_parallel_output(void)
{
    tf_begin("iot/多任务并行输出");

    s_parallel_len = 0;
    memset(s_parallel_log, 0, sizeof(s_parallel_log));

    TF_CHECK_INT(iot_task_create(&s_par1, "P1", 4, parallel_entry,
                                  (void *)(intptr_t)1,
                                  s_par_stack1, sizeof(s_par_stack1)), IOT_OK);
    TF_CHECK_INT(iot_task_create(&s_par2, "P2", 4, parallel_entry,
                                  (void *)(intptr_t)2,
                                  s_par_stack2, sizeof(s_par_stack2)), IOT_OK);
    TF_CHECK_INT(iot_task_create(&s_par3, "P3", 4, parallel_entry,
                                  (void *)(intptr_t)3,
                                  s_par_stack3, sizeof(s_par_stack3)), IOT_OK);

    iot_sleep_ms(1U);          /* 让三个任务交替执行 */

    s_parallel_log[s_parallel_len] = '\0';
    TF_CHECK_STR(s_parallel_log, "123123123");

    tf_end();
}

/* ============================== 对外入口 ============================== */

void iot_test_task(void)
{
    test_task_create();
    test_tick_sleep();
    test_round_robin();
    test_priority();
    test_task_exit();
    test_parallel_output();
}
