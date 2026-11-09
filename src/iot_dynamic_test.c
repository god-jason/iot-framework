/*
 * iot_dynamic_test.c - 内核对象动态创建/销毁测试
 *
 * 覆盖：任务、信号量、互斥量、事件组、消息队列、软件定时器的
 *       create/destroy 路径，以及销毁时的占用保护与参数校验。
 * 依赖：需在 iot_init() 之后运行（ut_iot 已完成初始化）。
 */

#include "test.h"
#include "iot.h"

#include <string.h>

static int s_timer_hits;

static void timer_cb(void *arg)
{
    int *hits = (int *)arg;

    if (hits != NULL) {
        (*hits)++;
    }
}

static void dyn_task_entry(void *arg)
{
    (void)arg;
}

void ut_dynamic(void)
{
    uint32_t flags = 0U;
    int value = 0;

    tf_begin("iot/dynamic 对象动态创建与销毁");

    /* -------- 信号量 -------- */
    {
        iot_sem_t *sem = iot_sem_create(1U);

        TF_CHECK(sem != NULL);
        if (sem != NULL) {
            TF_CHECK_INT(iot_sem_take(sem, IOT_TIMEOUT_NONE), IOT_OK);
            TF_CHECK_INT(iot_sem_take(sem, IOT_TIMEOUT_NONE), IOT_TIMEOUT);
            TF_CHECK_INT(iot_sem_give(sem), IOT_OK);
            TF_CHECK_INT(iot_sem_destroy(sem), IOT_OK);
        }
        TF_CHECK_INT(iot_sem_destroy(NULL), IOT_INVALID_PARAM);
    }

    /* -------- 互斥量 -------- */
    {
        iot_mutex_t *mutex = iot_mutex_create();

        TF_CHECK(mutex != NULL);
        if (mutex != NULL) {
            TF_CHECK_INT(iot_mutex_destroy(mutex), IOT_OK);
        }
        TF_CHECK_INT(iot_mutex_destroy(NULL), IOT_INVALID_PARAM);
    }

    /* -------- 事件组 -------- */
    {
        iot_event_t *event = iot_event_create();

        TF_CHECK(event != NULL);
        if (event != NULL) {
            TF_CHECK_INT(iot_event_set(event, 0x03U), IOT_OK);
            TF_CHECK_INT(iot_event_get(event, &flags), IOT_OK);
            TF_CHECK_INT(flags, 0x03U);
            TF_CHECK_INT(iot_event_clear(event, 0x01U), IOT_OK);
            TF_CHECK_INT(iot_event_get(event, &flags), IOT_OK);
            TF_CHECK_INT(flags, 0x02U);
            TF_CHECK_INT(iot_event_destroy(event), IOT_OK);
        }
        TF_CHECK_INT(iot_event_destroy(NULL), IOT_INVALID_PARAM);
    }

    /* -------- 消息队列 -------- */
    {
        iot_queue_t *queue = iot_queue_create(sizeof(int), 4U);

        TF_CHECK(queue != NULL);
        if (queue != NULL) {
            value = 42;
            TF_CHECK_INT(iot_queue_send(queue, &value, IOT_TIMEOUT_NONE), IOT_OK);
            /* 仍有消息：销毁被拒绝 */
            TF_CHECK_INT(iot_queue_destroy(queue), IOT_BUSY);

            value = 0;
            TF_CHECK_INT(iot_queue_recv(queue, &value, IOT_TIMEOUT_NONE), IOT_OK);
            TF_CHECK_INT(value, 42);
            TF_CHECK_INT(iot_queue_destroy(queue), IOT_OK);
        }
        TF_CHECK(iot_queue_create(0U, 4U) == NULL);
        TF_CHECK(iot_queue_create(4U, 0U) == NULL);
        TF_CHECK_INT(iot_queue_destroy(NULL), IOT_INVALID_PARAM);
    }

    /* -------- 软件定时器 -------- */
    {
        iot_timer_t *timer;

        s_timer_hits = 0;
        timer = iot_timer_create("dyn", timer_cb, &s_timer_hits, 100U, true);
        TF_CHECK(timer != NULL);
        if (timer != NULL) {
            TF_CHECK_INT(iot_timer_start(timer, 100U), IOT_OK);
            /* 运行中销毁：内部先停止摘链，再释放 */
            TF_CHECK_INT(iot_timer_destroy(timer), IOT_OK);
        }
        TF_CHECK(iot_timer_create("bad", NULL, NULL, 100U, false) == NULL);
        TF_CHECK(iot_timer_create("bad", timer_cb, NULL, 0U, false) == NULL);
    }

    /* -------- 任务 -------- */
    {
        iot_task_t *task = iot_task_create_dynamic("dyn", 5U, dyn_task_entry,
                                                   NULL, 1024U);

        TF_CHECK(task != NULL);
        if (task != NULL) {
            TF_CHECK_INT(iot_task_destroy(task), IOT_OK);
        }
        TF_CHECK(iot_task_create_dynamic("bad", 5U, NULL, NULL, 1024U) == NULL);
        TF_CHECK(iot_task_create_dynamic("bad", 5U, dyn_task_entry,
                                         NULL, 0U) == NULL);
        /* 最低优先级保留给空闲任务 */
        TF_CHECK(iot_task_create_dynamic("bad", IOT_PRIORITY_IDLE,
                                         dyn_task_entry, NULL, 1024U) == NULL);
        TF_CHECK_INT(iot_task_destroy(NULL), IOT_INVALID_PARAM);
        TF_CHECK_INT(iot_task_destroy(iot_idle_task), IOT_INVALID_PARAM);
    }

    tf_end();
}
