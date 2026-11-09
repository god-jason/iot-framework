/*
 * iot_idle.c - 内置空闲任务
 *
 * 空闲任务固定为最低优先级（IOT_PRIORITY_IDLE），永远就绪。
 * 没有其它任务可运行时执行平台提供的空闲处理：
 *   - PC 模拟端推进模拟时间，使 iot_sleep_ms() 快速确定性返回；
 *   - 单片机端执行 WFI 进入低功耗。
 */

#include "iot.h"
#include "iot_port.h"
#include "iot_assert.h"

#define IOT_IDLE_STACK_SIZE 4096

iot_task_t *iot_idle_task = NULL;

static iot_task_t s_idle_tcb;
static uint8_t s_idle_stack[IOT_IDLE_STACK_SIZE];

/* 空闲任务主体：平台空闲处理 + 让出 CPU */
static void idle_entry(void *arg)
{
    (void)arg;

    for (;;) {
#if IOT_USE_TICKLESS
        /* 按最近的唤醒/定时截止点休眠，避免固定 tick 周期性唤醒 */
        iot_port_idle_timeout(iot_idle_next_timeout());
#else
        iot_port_idle();
#endif
        iot_yield();
    }
}

void iot_idle_init(void)
{
    int rc;
    uint32_t i;

    s_idle_tcb.name = "idle";
    s_idle_tcb.priority = IOT_PRIORITY_IDLE;
    s_idle_tcb.base_priority = IOT_PRIORITY_IDLE;
    s_idle_tcb.entry = idle_entry;
    s_idle_tcb.arg = NULL;
    s_idle_tcb.stack_mem = s_idle_stack;
    s_idle_tcb.stack_size = sizeof(s_idle_stack);
    s_idle_tcb.state = IOT_TASK_READY;
    s_idle_tcb.created = true;
    s_idle_tcb.wait_queue = NULL;

#if IOT_USE_TIME_SLICING
    s_idle_tcb.slice_remaining = IOT_TIME_SLICE_TICKS;
#endif
#if IOT_USE_PRIORITY_INHERIT
    s_idle_tcb.wait_mutex = NULL;
#endif
#if IOT_USE_STACK_CHECK
    /* 整栈填充魔数，与用户任务保持一致的水位线语义 */
    for (i = 0U; i < sizeof(s_idle_stack) / sizeof(uint32_t); i++) {
        ((uint32_t *)s_idle_stack)[i] = IOT_STACK_MAGIC;
    }
#endif

    list_node_init(&s_idle_tcb.ready_node);
    list_node_init(&s_idle_tcb.delayed_node);
    list_node_init(&s_idle_tcb.wait_node);

    rc = iot_port_stack_init(&s_idle_tcb);
    if (rc != IOT_OK) {
        LOG_E(IOT_TAG, "idle: port stack init failed: %d", rc);
        IOT_ASSERT(0);
        return;
    }

    iot_idle_task = &s_idle_tcb;
    iot_make_ready(&s_idle_tcb);
    iot_task_register(&s_idle_tcb);

    LOG_I(IOT_TAG, "idle task created, stack=%u bytes",
              (unsigned)sizeof(s_idle_stack));
}
