/*
 * iot_timers.c - 软件定时器（单次 / 周期）
 *
 * 定时器按 deadline 升序挂入 s_timers，由 iot_tick_increment() 触发。
 * 回调执行上下文：
 *   - Windows 模拟端：空闲任务（普通任务上下文）；
 *   - STM32 端：SysTick 中断（ISR 上下文）。
 * 因此回调必须短小，不得阻塞或长时间占用 CPU。
 */

#include "iot.h"
#include "iot_port.h"
#include "iot_mem.h"

#include <string.h>

static list_t s_timers;

#if IOT_USE_TIMER_TASK
/*
 * 任务模式：tick 只把到期定时器挂入 s_pending，再由定时器任务执行回调，
 * 避免回调在 SysTick 中断里运行（可安全使用阻塞 API）。
 */
static list_t s_pending;
static iot_sem_t s_timer_sem;
static iot_task_t s_timer_task;
static uint8_t s_timer_stack[IOT_TIMER_TASK_STACK_SIZE];
static bool s_timer_task_created;

static void iot_timer_task_entry(void *arg);

/* 惰性创建定时器任务：首次 iot_timer_start 时调用 */
static int iot_timer_task_ensure(void)
{
    int rc;

    if (s_timer_task_created) {
        return IOT_OK;
    }
    if (iot_in_isr()) {
        /* 中断里无法构造任务上下文，要求定时器在任务上下文中启动 */
        LOG_E(IOT_TAG, "timer_start: cannot create timer task in ISR");
        return IOT_FORBIDDEN_IN_ISR;
    }

    rc = iot_task_create(&s_timer_task, "timer", IOT_TIMER_TASK_PRIORITY,
                         iot_timer_task_entry, NULL,
                         s_timer_stack, sizeof(s_timer_stack));
    if (rc == IOT_OK) {
        s_timer_task_created = true;
    }
    return rc;
}

/* 定时器任务：等待信号量，逐个执行到期回调 */
static void iot_timer_task_entry(void *arg)
{
    (void)arg;

    for (;;) {
        (void)iot_sem_take(&s_timer_sem, IOT_TIMEOUT_FOREVER);

        for (;;) {
            iot_timer_cb_t cb;
            void *cb_arg;
            list_node_t *node;

            iot_port_critical_enter();
            node = list_pop_first(&s_pending);
            if (node == NULL) {
                iot_port_critical_exit();
                break;
            }
            {
                iot_timer_t *timer = IOT_CONTAINER_OF(node, iot_timer_t,
                                                      pending_node);

                timer->pending = false;
                cb = timer->cb;
                cb_arg = timer->arg;
            }
            iot_port_critical_exit();

            if (cb != NULL) {
                cb(cb_arg);
            }
        }
    }
}
#endif /* IOT_USE_TIMER_TASK */

/*
 * 按 deadline 插入。常见场景是新 deadline 晚于队尾，直接尾插 O(1)；
 * 只有乱序（重启短周期定时器等）才回退到线性有序插入。
 */
static void iot_timer_insert(list_node_t *node)
{
    list_node_t *tail = s_timers.head.prev;

    if (tail == &s_timers.head ||
        !IOT_TICK_AFTER(tail->sort_key, node->sort_key)) {
        list_add_tail(&s_timers, node);
        return;
    }
    list_insert_sorted(&s_timers, node);
}

/* 初始化定时器链表、待执行队列与定时器任务信号量（由 iot_init 调用）。 */
void iot_timers_init(void)
{
    list_init(&s_timers);

#if IOT_USE_TIMER_TASK
    list_init(&s_pending);
    (void)iot_sem_init(&s_timer_sem, 0U);
    s_timer_task_created = false;
#endif
}

/* 初始化定时器对象：记录回调、周期与单次/周期属性，初始为未启动。 */
int iot_timer_init(iot_timer_t *timer, const char *name,
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
    timer->initialized = true;
    list_node_init(&timer->node);
    list_node_init(&timer->pending_node);

    LOG_D(IOT_TAG, "timer init: %s period=%u periodic=%d",
              name != NULL ? name : "?", (unsigned)period_ms, (int)periodic);
    return IOT_OK;
}

/*
 * 启动或重启定时器：
 * 任务模式下首次调用会惰性创建 timer 任务；随后按 deadline 插入定时链表。
 */
int iot_timer_start(iot_timer_t *timer, uint32_t period_ms)
{
#if IOT_USE_TIMER_TASK
    int rc;
#endif

    if (!IOT_OBJ_VALID(timer) || period_ms == 0U) {
        LOG_E(IOT_TAG, "timer_start: invalid param");
        return IOT_INVALID_PARAM;
    }

#if IOT_USE_TIMER_TASK
    rc = iot_timer_task_ensure();
    if (rc != IOT_OK) {
        return rc;
    }
#endif

    iot_port_critical_enter();
    if (timer->running) {
        list_remove(&timer->node);   /* 重启：先摘除旧节点 */
    }
    timer->period = period_ms;
    timer->deadline = iot_tick_count + period_ms;
    timer->node.sort_key = timer->deadline;
    iot_timer_insert(&timer->node);
    timer->running = true;
    iot_port_critical_exit();

    LOG_D(IOT_TAG, "timer start: %s period=%u",
              timer->name != NULL ? timer->name : "?", (unsigned)period_ms);
    return IOT_OK;
}

/* 停止定时器：从定时链表摘除，并取消尚未执行的到期回调。 */
int iot_timer_stop(iot_timer_t *timer)
{
    if (!IOT_OBJ_VALID(timer)) {
        LOG_E(IOT_TAG, "timer_stop: invalid timer");
        return IOT_INVALID_PARAM;
    }

    iot_port_critical_enter();
    if (timer->running) {
        list_remove(&timer->node);
        timer->running = false;
    }
#if IOT_USE_TIMER_TASK
    /* 已到期但回调尚未执行：一并取消，避免停止后仍触发一次 */
    if (timer->pending) {
        list_remove(&timer->pending_node);
        timer->pending = false;
    }
#endif
    iot_port_critical_exit();

    LOG_D(IOT_TAG, "timer stop: %s",
              timer->name != NULL ? timer->name : "?");
    return IOT_OK;
}

/* 返回最近一个定时器的到期时刻；没有定时器返回 UINT32_MAX。 */
uint32_t iot_timer_next_deadline(void)
{
    list_node_t *node = list_first(&s_timers);

    if (node == NULL) {
        return UINT32_MAX;
    }
    return IOT_CONTAINER_OF(node, iot_timer_t, node)->deadline;
}

/*
 * tick 驱动入口：摘出所有到期定时器。
 * 任务模式：入待执行队列并唤醒 timer 任务；兼容模式：直接回调。
 */
void iot_timer_tick(uint32_t now)
{
#if IOT_USE_TIMER_TASK
    bool wake = false;
#endif

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
            iot_timer_insert(&timer->node);
        } else {
            timer->running = false;
        }

#if IOT_USE_TIMER_TASK
        /* 同一周期内重复到期时只保留一次待执行，避免回调堆积 */
        if (!timer->pending) {
            timer->pending = true;
            list_add_tail(&s_pending, &timer->pending_node);
            wake = true;
        }
        iot_port_critical_exit();
#else
        iot_port_critical_exit();
#endif

        IOT_TRACE_OBJ(IOT_EVT_TIMER_FIRE, (uint32_t)(uintptr_t)timer,
                      timer->periodic ? 1U : 0U);
        LOG_D(IOT_TAG, "timer fire: %s deadline=%u",
                  timer->name != NULL ? timer->name : "?", (unsigned)now);

#if !IOT_USE_TIMER_TASK
        if (timer->cb != NULL) {
            timer->cb(timer->arg);   /* 兼容模式：回调在 tick 上下文执行 */
        }
#endif
    }

#if IOT_USE_TIMER_TASK
    if (wake) {
        /* 可在 ISR 中调用：唤醒定时器任务，由任务上下文执行回调 */
        (void)iot_sem_give(&s_timer_sem);
    }
#endif
}

/* 动态创建定时器：对象由 iot_mem_calloc 分配，参数非法或失败返回 NULL。 */
iot_timer_t *iot_timer_create(const char *name, iot_timer_cb_t cb, void *arg,
                              uint32_t period_ms, bool periodic)
{
    iot_timer_t *timer = (iot_timer_t *)iot_mem_calloc(1U, sizeof(*timer));

    if (timer == NULL) {
        LOG_E(IOT_TAG, "timer_create: no memory");
        return NULL;
    }
    if (iot_timer_init(timer, name, cb, arg, period_ms, periodic) != IOT_OK) {
        iot_mem_free(timer);
        return NULL;
    }
    return timer;
}

/* 销毁动态定时器：先停止并摘链，避免释放后仍被 tick 访问，再释放内存。 */
int iot_timer_destroy(iot_timer_t *timer)
{
    if (!IOT_OBJ_VALID(timer)) {
        return IOT_INVALID_PARAM;
    }

    (void)iot_timer_stop(timer);    /* 运行中先摘链，避免释放后仍被 tick 访问 */
    timer->initialized = false;
    iot_mem_free(timer);
    return IOT_OK;
}