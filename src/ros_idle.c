/*
 * ros_idle.c - 内置空闲任务
 *
 * 空闲任务固定为最低优先级（ROS_PRIORITY_IDLE），永远就绪。
 * 没有其它任务可运行时执行平台提供的空闲处理：
 *   - PC 模拟端推进模拟时间，使 ros_sleep_ms() 快速确定性返回；
 *   - 单片机端执行 WFI 进入低功耗。
 */

#include "ros.h"
#include "common.h"
#include "ros_port.h"
#include "ros_assert.h"

#define ROS_IDLE_STACK_SIZE 1024

ros_task_t *ros_idle_task = NULL;

static ros_task_t s_idle_tcb;
static uint8_t s_idle_stack[ROS_IDLE_STACK_SIZE];

/* 空闲任务主体：平台空闲处理 + 让出 CPU */
static void idle_entry(void *arg)
{
    (void)arg;

    for (;;) {
        ROS_TRACE_DBG(ROS_EVT_IDLE_ENTER, ros_tick_get(), 0U);
#if ROS_USE_TICKLESS
        /* 按最近的唤醒/定时截止点休眠，避免固定 tick 周期性唤醒 */
        {
            uint32_t timeout = ros_idle_next_timeout();

            ROS_TRACE_DBG(ROS_EVT_TICKLESS_ENTER, timeout, 0U);
            ros_port_idle_timeout(timeout);
            ROS_TRACE_DBG(ROS_EVT_TICKLESS_EXIT, ros_tick_get(), 0U);
        }
#else
        ros_port_idle();
#endif
        ROS_TRACE_DBG(ROS_EVT_IDLE_EXIT, ros_tick_get(), 0U);
        ros_yield();
    }
}

/*
 * 初始化内置空闲任务：优先级最低、栈静态分配，
 * 填充栈魔数并构造移植层上下文（不加入普通任务注册表）。
 */
void ros_idle_init(void)
{
    int rc;
    uint32_t i;

    s_idle_tcb.name = "idle";
    s_idle_tcb.priority = ROS_PRIORITY_IDLE;
    s_idle_tcb.base_priority = ROS_PRIORITY_IDLE;
    s_idle_tcb.entry = idle_entry;
    s_idle_tcb.arg = NULL;
    s_idle_tcb.stack_mem = s_idle_stack;
    s_idle_tcb.stack_size = sizeof(s_idle_stack);
    s_idle_tcb.state = ROS_TASK_READY;
    s_idle_tcb.created = true;
    s_idle_tcb.wait_queue = NULL;

#if ROS_USE_TIME_SLICING
    s_idle_tcb.slice_remaining = ROS_TIME_SLICE_TICKS;
#endif
#if ROS_USE_PRIORITY_INHERIT
    s_idle_tcb.wait_mutex = NULL;
#endif
#if ROS_USE_STACK_CHECK
    /* 整栈填充魔数，与用户任务保持一致的水位线语义 */
    for (i = 0U; i < sizeof(s_idle_stack) / sizeof(uint32_t); i++) {
        ((uint32_t *)s_idle_stack)[i] = ROS_STACK_MAGIC;
    }
#endif

    list_node_init(&s_idle_tcb.ready_node);
    list_node_init(&s_idle_tcb.delayed_node);
    list_node_init(&s_idle_tcb.wait_node);

    rc = ros_port_stack_init(&s_idle_tcb);
    if (rc != ROS_OK) {
        LOG_E(ROS_TAG, "idle: port stack init failed: %d", rc);
        ROS_ASSERT(0);
        return;
    }

    ros_idle_task = &s_idle_tcb;
    ros_make_ready(&s_idle_tcb);
    ros_task_register(&s_idle_tcb);

    LOG_I(ROS_TAG, "idle task created, stack=%u bytes",
              (unsigned)sizeof(s_idle_stack));
}
