/*
 * ros_dynamic_test.c - 内核对象动态创建/销毁测试
 *
 * 覆盖：任务、信号量、互斥量、事件组、消息队列、软件定时器的
 *       create/destroy 路径，以及销毁时的占用保护与参数校验。
 * 依赖：需在 ros_init() 之后运行（ut_ros 已完成初始化）。
 */

#include "test.h"
#include "ros.h"

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

    tf_begin("ros/dynamic 对象动态创建与销毁");

    /* -------- 信号量 -------- */
    {
        ros_sem_t *sem = ros_sem_create(1U);

        TF_CHECK(sem != NULL);
        if (sem != NULL) {
            TF_CHECK_INT(ros_sem_take(sem, ROS_TIMEOUT_NONE), ROS_OK);
            TF_CHECK_INT(ros_sem_take(sem, ROS_TIMEOUT_NONE), ROS_TIMEOUT);
            TF_CHECK_INT(ros_sem_give(sem), ROS_OK);
            TF_CHECK_INT(ros_sem_destroy(sem), ROS_OK);
        }
        TF_CHECK_INT(ros_sem_destroy(NULL), ROS_INVALID_PARAM);
    }

    /* -------- 互斥量 -------- */
    {
        ros_mutex_t *mutex = ros_mutex_create();

        TF_CHECK(mutex != NULL);
        if (mutex != NULL) {
            TF_CHECK_INT(ros_mutex_destroy(mutex), ROS_OK);
        }
        TF_CHECK_INT(ros_mutex_destroy(NULL), ROS_INVALID_PARAM);
    }

    /* -------- 事件组 -------- */
    {
        ros_event_t *event = ros_event_create();

        TF_CHECK(event != NULL);
        if (event != NULL) {
            TF_CHECK_INT(ros_event_set(event, 0x03U), ROS_OK);
            TF_CHECK_INT(ros_event_get(event, &flags), ROS_OK);
            TF_CHECK_INT(flags, 0x03U);
            TF_CHECK_INT(ros_event_clear(event, 0x01U), ROS_OK);
            TF_CHECK_INT(ros_event_get(event, &flags), ROS_OK);
            TF_CHECK_INT(flags, 0x02U);
            TF_CHECK_INT(ros_event_destroy(event), ROS_OK);
        }
        TF_CHECK_INT(ros_event_destroy(NULL), ROS_INVALID_PARAM);
    }

    /* -------- 消息队列 -------- */
    {
        ros_queue_t *queue = ros_queue_create(sizeof(int), 4U);

        TF_CHECK(queue != NULL);
        if (queue != NULL) {
            value = 42;
            TF_CHECK_INT(ros_queue_send(queue, &value, ROS_TIMEOUT_NONE), ROS_OK);
            /* 仍有消息：销毁被拒绝 */
            TF_CHECK_INT(ros_queue_destroy(queue), ROS_BUSY);

            value = 0;
            TF_CHECK_INT(ros_queue_recv(queue, &value, ROS_TIMEOUT_NONE), ROS_OK);
            TF_CHECK_INT(value, 42);
            TF_CHECK_INT(ros_queue_destroy(queue), ROS_OK);
        }
        TF_CHECK(ros_queue_create(0U, 4U) == NULL);
        TF_CHECK(ros_queue_create(4U, 0U) == NULL);
        TF_CHECK_INT(ros_queue_destroy(NULL), ROS_INVALID_PARAM);
    }

    /* -------- 软件定时器 -------- */
    {
        ros_timer_t *timer;

        s_timer_hits = 0;
        timer = ros_timer_create("dyn", timer_cb, &s_timer_hits, 100U, true);
        TF_CHECK(timer != NULL);
        if (timer != NULL) {
            TF_CHECK_INT(ros_timer_start(timer, 100U), ROS_OK);
            /* 运行中销毁：内部先停止摘链，再释放 */
            TF_CHECK_INT(ros_timer_destroy(timer), ROS_OK);
        }
        TF_CHECK(ros_timer_create("bad", NULL, NULL, 100U, false) == NULL);
        TF_CHECK(ros_timer_create("bad", timer_cb, NULL, 0U, false) == NULL);
    }

    /* -------- 任务 -------- */
    {
        ros_task_t *task = ros_task_create_dynamic("dyn", 5U, dyn_task_entry,
                                                   NULL, 1024U);

        TF_CHECK(task != NULL);
        if (task != NULL) {
            TF_CHECK_INT(ros_task_destroy(task), ROS_OK);
        }
        TF_CHECK(ros_task_create_dynamic("bad", 5U, NULL, NULL, 1024U) == NULL);
        TF_CHECK(ros_task_create_dynamic("bad", 5U, dyn_task_entry,
                                         NULL, 0U) == NULL);
        /* 最低优先级保留给空闲任务 */
        TF_CHECK(ros_task_create_dynamic("bad", ROS_PRIORITY_IDLE,
                                         dyn_task_entry, NULL, 1024U) == NULL);
        TF_CHECK_INT(ros_task_destroy(NULL), ROS_INVALID_PARAM);
        TF_CHECK_INT(ros_task_destroy(ros_idle_task), ROS_INVALID_PARAM);
    }

    tf_end();
}
