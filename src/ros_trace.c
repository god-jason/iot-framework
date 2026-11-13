/*
 * ros_trace.c - 事件流式发射（无内存快照、无环形缓冲）
 *
 * 事件产生即格式化成一行文本，直接交给 ros_port_trace_write()：
 *   PC : 写入文件（默认 ros_trace.log，可用 ros_trace_start_file 指定）；
 *   MCU: 由指定串口发出。
 *
 * 约束（由 RTOS 上下文决定）：
 *   - emit 可能来自任务、SysTick ISR、PendSV、崩溃路径；
 *   - 不做动态分配、不进临界区、不写日志、不触发断言；
 *   - 只用栈上一行缓冲 + GCC 原子内建做重入保护，全程 O(1)、无锁。
 */

#include "ros.h"
#include "common.h"
#include "ros_port.h"
#include "ros_trace.h"

/* 默认 µs 时间戳；STM32 平台用 DWT->CYCCNT 提供 strong 实现覆盖 */
__attribute__((weak)) uint32_t ros_port_trace_time_us(void)
{
    return ros_tick_get() * 1000U;
}

/* 默认丢弃输出；各平台目录下的 trace 实现文件提供 strong 覆盖 */
__attribute__((weak)) void ros_port_trace_write(const char *data, uint32_t len)
{
    (void)data;
    (void)len;
}

#if ROS_USE_TRACE

volatile uint8_t g_ros_trace_level = (uint8_t)ROS_TRACE_DEFAULT_LEVEL;

static volatile uint32_t s_count;
static volatile uint32_t s_dropped;
static volatile int      s_in_emit;
static volatile int      s_frozen;
static ros_trace_clock_fn s_clock;

/* ------------------------------ 事件名表 ------------------------------ */

static const char *const s_core_names[] = {
    "switch", "task_ready", "task_block", "task_wake", "task_timeout",
    "task_create", "task_destroy", "task_exit", "task_sleep", "prio_change",
    "tick", "slice_expire", "preempt", "idle_enter", "idle_exit",
    "task_suspend", "task_resume", "tickless_enter", "tickless_exit"
};
static const char *const s_obj_names[] = {
    "sem_take", "sem_give", "mutex_lock", "mutex_unlock",
    "queue_send", "queue_recv", "msgq_send", "msgq_recv",
    "sbuf_write", "sbuf_read", "event_set", "event_wait",
    "notify", "timer_fire"
};
static const char *const s_isr_names[] = {
    "isr_enter", "isr_exit", "from_isr"
};
static const char *const s_fault_names[] = {
    "assert", "crash", "stack_overflow"
};
static const char *const s_sys_names[] = {
    "marker", "trace_overflow", "time_sync", "name_begin", "name_chunk"
};

const char *ros_trace_event_name(uint16_t id)
{
    const char *const *table;
    uint32_t count;
    uint8_t seq = ROS_TRACE_SEQ(id);

    switch (ROS_TRACE_GROUP(id)) {
    case ROS_TRACE_GRP_CORE:
        table = s_core_names;  count = sizeof(s_core_names) / sizeof(s_core_names[0]);  break;
    case ROS_TRACE_GRP_OBJ:
        table = s_obj_names;   count = sizeof(s_obj_names) / sizeof(s_obj_names[0]);   break;
    case ROS_TRACE_GRP_ISR:
        table = s_isr_names;   count = sizeof(s_isr_names) / sizeof(s_isr_names[0]);   break;
    case ROS_TRACE_GRP_FAULT:
        table = s_fault_names; count = sizeof(s_fault_names) / sizeof(s_fault_names[0]); break;
    case ROS_TRACE_GRP_SYS:
        table = s_sys_names;   count = sizeof(s_sys_names) / sizeof(s_sys_names[0]);   break;
    default:
        return "unknown";
    }
    return (seq < count) ? table[seq] : "unknown";
}

/* ------------------------------ 轻量格式化（零 libc 依赖） ------------------------------ */

static void put(char *buf, uint32_t cap, uint32_t *n, const char *s)
{
    while ((*s != '\0') && (*n < cap)) {
        buf[(*n)++] = *s++;
    }
}

static void dec(char *buf, uint32_t cap, uint32_t *n, uint32_t v)
{
    char tmp[10];
    uint32_t k = 0U;

    if (v == 0U) {
        put(buf, cap, n, "0");
        return;
    }
    while (v > 0U) {
        tmp[k++] = (char)('0' + (v % 10U));
        v /= 10U;
    }
    while (k > 0U) {
        k--;
        if (*n < cap) {
            buf[(*n)++] = tmp[k];
        }
    }
}

static void hex8(char *buf, uint32_t cap, uint32_t *n, uint32_t v)
{
    static const char digits[] = "0123456789ABCDEF";
    int shift;

    for (shift = 28; shift >= 0; shift -= 4) {
        if (*n < cap) {
            buf[(*n)++] = digits[(v >> (uint32_t)shift) & 0x0FU];
        }
    }
}

/* ------------------------------ 单行输出 ------------------------------ */

/*
 * 文本行格式（一行一次输出）：
 *   T <ts_us> <evt_name> task=0x<指针> v1=<十进制> v2=<十进制> [isr=1]\r\n
 * 时间戳为微秒，事件名来自事件名表，ISR 标记只出现在中断上下文产生的事件上。
 */
static void ros_trace_emit_line(uint16_t evt, uint32_t ts, uint32_t task,
                                uint32_t v1, uint32_t v2)
{
    char line[160];
    uint32_t n = 0U;

    put(line, sizeof(line), &n, "T ");
    dec(line, sizeof(line), &n, ts);
    put(line, sizeof(line), &n, " ");
    put(line, sizeof(line), &n, ros_trace_event_name(evt));
    put(line, sizeof(line), &n, " task=0x");
    hex8(line, sizeof(line), &n, task);
    put(line, sizeof(line), &n, " v1=");
    dec(line, sizeof(line), &n, v1);
    put(line, sizeof(line), &n, " v2=");
    dec(line, sizeof(line), &n, v2);
    if (ROS_TRACE_IN_ISR(evt)) {
        put(line, sizeof(line), &n, " isr=1");
    }
    put(line, sizeof(line), &n, "\r\n");

    ros_port_trace_write(line, n);
}

/* ------------------------------ 生命周期 ------------------------------ */

void ros_trace_init(void)
{
    s_count = 0U;
    s_dropped = 0U;
    s_in_emit = 0;
    s_frozen = 0;
    s_clock = NULL;
}

void ros_trace_emit(uint16_t evt, uint32_t v1, uint32_t v2)
{
    uint32_t ts;
    uint32_t task;

    if (__sync_lock_test_and_set(&s_in_emit, 1) != 0) {
        (void)__sync_fetch_and_add(&s_dropped, 1U);
        return;
    }
    if (s_frozen) {
        (void)__sync_fetch_and_add(&s_dropped, 1U);
        __sync_lock_release(&s_in_emit);
        return;
    }

    if (ros_port_in_isr()) {
        evt |= ROS_TRACE_FLAG_ISR;
    }
    ts = (s_clock != NULL) ? s_clock() : ros_port_trace_time_us();
    task = (uint32_t)(uintptr_t)ros_current_task;

    (void)__sync_fetch_and_add(&s_count, 1U);
    ros_trace_emit_line(evt, ts, task, v1, v2);

    __sync_lock_release(&s_in_emit);
}

void ros_trace_enable(void)
{
    ros_trace_set_level(ROS_TRACE_LEVEL_DEBUG);
}

void ros_trace_disable(void)
{
    ros_trace_set_level(ROS_TRACE_LEVEL_OFF);
}

void ros_trace_reset(void)
{
    s_count = 0U;
    s_dropped = 0U;
}

void ros_trace_set_clock(ros_trace_clock_fn fn)
{
    s_clock = fn;
}

/* ------------------------------ 名称映射 ------------------------------ */

void ros_trace_name(const void *obj, uint8_t type, const char *name)
{
    uint32_t len = 0U;
    uint32_t i;

    if ((obj == NULL) || (name == NULL) ||
        (g_ros_trace_level > ROS_TRACE_LEVEL_CORE)) {
        return;
    }
    while ((name[len] != '\0') && (len < 64U)) {
        len++;
    }

    ros_trace_emit(ROS_EVT_NAME_BEGIN, (uint32_t)(uintptr_t)obj,
                   (uint32_t)type | (len << 8));
    for (i = 0U; i < len; i += 4U) {
        uint32_t chunk = 0U;
        uint32_t j;

        for (j = 0U; (j < 4U) && ((i + j) < len); j++) {
            chunk |= ((uint32_t)(uint8_t)name[i + j]) << (j * 8U);
        }
        ros_trace_emit(ROS_EVT_NAME_CHUNK, chunk, i / 4U);
    }
}

/* ------------------------------ 冻结 / 计数 ------------------------------ */

void ros_trace_freeze(void)
{
    s_frozen = 1;   /* 事件已流式输出，冻结只是停止后续输出 */
}
void ros_trace_unfreeze(void) { s_frozen = 0; }
bool ros_trace_is_frozen(void) { return s_frozen != 0; }
uint32_t ros_trace_count(void) { return s_count; }
uint32_t ros_trace_dropped(void) { return s_dropped; }

#else /* ROS_USE_TRACE：编译期裁剪，全部为空实现 */

volatile uint8_t g_ros_trace_level = (uint8_t)ROS_TRACE_LEVEL_OFF;

void ros_trace_init(void) {}
void ros_trace_emit(uint16_t evt, uint32_t v1, uint32_t v2)
{
    (void)evt; (void)v1; (void)v2;
}
void ros_trace_enable(void) {}
void ros_trace_disable(void) {}
void ros_trace_reset(void) {}
void ros_trace_set_clock(ros_trace_clock_fn fn) { (void)fn; }
void ros_trace_name(const void *obj, uint8_t type, const char *name)
{
    (void)obj; (void)type; (void)name;
}
void ros_trace_freeze(void) {}
void ros_trace_unfreeze(void) {}
bool ros_trace_is_frozen(void) { return false; }
uint32_t ros_trace_count(void) { return 0U; }
uint32_t ros_trace_dropped(void) { return 0U; }
const char *ros_trace_event_name(uint16_t id)
{
    (void)id;
    return "trace_off";
}

#endif /* ROS_USE_TRACE */
