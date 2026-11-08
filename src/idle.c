/*
 * idle.c - 内置空闲任务
 *
 * 空闲任务固定为最低优先级（IOT_PRIORITY_IDLE），永远就绪。
 * 在 Windows 模拟端，它负责把模拟时间推进到最近的唤醒/定时点，
 * 使 iot_sleep_ms() 等调用可以确定性地快速返回；
 * 在单片机端可替换为低功耗指令（WFI）。
 */

#include "internal.h"
#include "port.h"

#define IOT_IDLE_STACK_SIZE 4096

iot_task_t *iot_idle_task = NULL;

static iot_task_t s_idle_tcb;
static uint8_t s_idle_stack[IOT_IDLE_STACK_SIZE];

void iot_idle_hook(void)
{
    uint32_t now = iot_tick_count;
    uint32_t delayed_deadline = iot_delayed_next_deadline();
    uint32_t timer_deadline = iot_timer_next_deadline();
    uint32_t next = (delayed_deadline < timer_deadline)
                    ? delayed_deadline : timer_deadline;

    /*
     * 推进到下一个时间点：
     *   - 两者都没有：每次只推进 1 tick，避免空转过快；
     *   - 有未来时间点：直接跳到该时刻；
     *   - 已到期：推进 1 tick 触发处理。
     */
    if (next == UINT32_MAX) {
        iot_tick_increment(1U);
    } else if (next > now) {
        iot_tick_increment(next - now);
    } else {
        iot_tick_increment(1U);
    }
}

/* 空闲任务主体：推进时间后让出，等待其它任务就绪 */
static void idle_entry(void *arg)
{
    (void)arg;

    for (;;) {
        iot_idle_hook();
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

    LOG_I("iot", "idle task created, stack=%u bytes",
              (unsigned)sizeof(s_idle_stack));
}