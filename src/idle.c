/*
 * idle.c - 内置空闲任务
 *
 * 空闲任务固定为最低优先级（IOT_PRIORITY_IDLE），永远就绪。
 * 没有其它任务可运行时执行平台提供的 iot_port_idle()：
 *   - PC 模拟端推进模拟时间，使 iot_sleep_ms() 快速确定性返回；
 *   - 单片机端执行 WFI 进入低功耗。
 */

#include "iot.h"
#include "port.h"

#define IOT_IDLE_STACK_SIZE 4096

iot_task_t *iot_idle_task = NULL;

static iot_task_t s_idle_tcb;
static uint8_t s_idle_stack[IOT_IDLE_STACK_SIZE];

/* 空闲任务主体：平台空闲处理 + 让出 CPU */
static void idle_entry(void *arg)
{
    (void)arg;

    for (;;) {
        iot_port_idle();
        iot_yield();
    }
}

void iot_idle_init(void)
{
    s_idle_tcb.name = "idle";
    s_idle_tcb.priority = IOT_PRIORITY_IDLE;
    s_idle_tcb.entry = idle_entry;
    s_idle_tcb.arg = NULL;
    s_idle_tcb.stack_mem = s_idle_stack;
    s_idle_tcb.stack_size = sizeof(s_idle_stack);
    s_idle_tcb.state = IOT_TASK_READY;
    s_idle_tcb.created = true;

    list_node_init(&s_idle_tcb.ready_node);
    list_node_init(&s_idle_tcb.delayed_node);
    list_node_init(&s_idle_tcb.wait_node);

    iot_port_stack_init(&s_idle_tcb);

    iot_idle_task = &s_idle_tcb;
    iot_make_ready(&s_idle_tcb);

    LOG_I(IOT_TAG, "idle task created, stack=%u bytes",
              (unsigned)sizeof(s_idle_stack));
}