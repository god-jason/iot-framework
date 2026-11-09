/*
 * timers.c - 软件定时器（单次 / 周期）
 *
 * 定时器按 deadline 升序挂入 s_timers，由 iot_tick_increment() 触发。
 * 回调执行上下文：
 *   - Windows 模拟端：空闲任务（普通任务上下文）；
 *   - STM32 端：SysTick 中断（ISR 上下文）。
 * 因此回调必须短小，不得阻塞或长时间占用 CPU。
 */

#include "iot.h"
#include "port.h"

#include <string.h>

static list_t s_timers;

void iot_timers_init(void)
{
    list_init(&s_timers);
}

iot_status_t iot_timer_init(iot_timer_t *timer, const char *name,
                              iot_timer_cb_t cb, void *arg,
                              uint32_t period_ms, bool periodic)
{
    if (timer == NULL || cb == NULL || period_ms == 0U) {
        LOG_E(IOT_TAG, "timer_init: invalid param");
        return IOT_INVALID_PARAM;
    }

    memset(timer, 0, sizeof(*timer));
    timer->name = name;
    timer->cb = cb;
    timer->arg = arg;
    timer->period = period_ms;
    timer->periodic = periodic;
    timer->running = false;
    list_node_init(&timer->node);

    LOG_D(IOT_TAG, "timer init: %s period=%u periodic=%d",
              name != NULL ? name : "?", (unsigned)period_ms, (int)periodic);
    return IOT_OK;
}

iot_status_t iot_timer_start(iot_timer_t *timer, uint32_t period_ms)
{
    if (timer == NULL || timer->cb == NULL || period_ms == 0U) {
        LOG_E(IOT_TAG, "timer_start: invalid param");
        return IOT_INVALID_PARAM;
    }

    iot_port_critical_enter();
    if (timer->running) {
        list_remove(&timer->node);   /* 重启：先摘除旧节点 */
    }
    timer->period = period_ms;
    timer->deadline = iot_tick_count + period_ms;
    timer->node.sort_key = timer->deadline;
    list_insert_sorted(&s_timers, &timer->node);
    timer->running = true;
    iot_port_critical_exit();

    LOG_D(IOT_TAG, "timer start: %s period=%u",
              timer->name != NULL ? timer->name : "?", (unsigned)period_ms);
    return IOT_OK;
}

iot_status_t iot_timer_stop(iot_timer_t *timer)
{
    if (timer == NULL) {
        LOG_E(IOT_TAG, "timer_stop: null timer");
        return IOT_INVALID_PARAM;
    }

    iot_port_critical_enter();
    if (timer->running) {
        list_remove(&timer->node);
        timer->running = false;
    }
    iot_port_critical_exit();

    LOG_D(IOT_TAG, "timer stop: %s",
              timer->name != NULL ? timer->name : "?");
    return IOT_OK;
}

uint32_t iot_timer_next_deadline(void)
{
    list_node_t *node = list_first(&s_timers);

    if (node == NULL) {
        return UINT32_MAX;
    }
    return IOT_CONTAINER_OF(node, iot_timer_t, node)->deadline;
}

void iot_timer_tick(uint32_t now)
{
    for (;;) {
        iot_timer_t *timer;
        list_node_t *node;

        iot_port_critical_enter();
        node = list_first(&s_timers);
        if (node == NULL || IOT_TICK_AFTER(node->sort_key, now)) {
            iot_port_critical_exit();
            break;      /* 没有到期定时器 */
        }

        timer = IOT_CONTAINER_OF(node, iot_timer_t, node);
        list_remove(&timer->node);

        if (timer->periodic) {
            /* 周期定时器：按原周期推后，避免累积漂移 */
            timer->deadline += timer->period;
            if (!IOT_TICK_AFTER(timer->deadline, now)) {
                timer->deadline = now + timer->period;
            }
            timer->node.sort_key = timer->deadline;
            list_insert_sorted(&s_timers, &timer->node);
        } else {
            timer->running = false;
        }
        iot_port_critical_exit();

        LOG_D(IOT_TAG, "timer fire: %s deadline=%u",
                  timer->name != NULL ? timer->name : "?", (unsigned)now);

        if (timer->cb != NULL) {
            timer->cb(timer->arg);   /* 回调在调用者上下文执行 */
        }
    }
}