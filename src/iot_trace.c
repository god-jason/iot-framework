/*
 * iot_trace.c - 事件发射与环形快照
 *
 * 约束（由 RTOS 上下文决定）：
 *   - emit 可能来自任务、SysTick ISR、PendSV、崩溃路径；
 *   - 因此不做动态分配、不进临界区、不写日志、不触发断言；
 *   - 用 GCC 原子内建做「预留槽位 + 重入保护」，全程 O(1)、无锁；
 *   - 重入/并发时只累加 dropped，绝不递归调用后端。
 *
 * 快照环覆盖写最旧事件；后端（barectf）另行产出标准 CTF 流。
 */

#include "iot.h"
#include "iot_port.h"
#include "iot_trace.h"

#if defined(IOT_PLATFORM_PC)
#include <stdio.h>
#endif

/*
 * 默认 µs 时间戳：毫秒 tick 换算。STM32 平台用 DWT->CYCCNT 提供 strong
 * 实现覆盖；weak 让 PC/AVR 等平台无需各写一份。
 */
__attribute__((weak)) uint32_t iot_port_trace_time_us(void)
{
    return iot_tick_get() * 1000U;
}

#if IOT_USE_TRACE

/* 运行期等级，默认关闭（由 iot_trace_init 再次确认） */
volatile uint8_t g_iot_trace_level = (uint8_t)IOT_TRACE_DEFAULT_LEVEL;

/* 编译期环形容量必须是 2 的幂 */
#if (IOT_TRACE_BUFFER_SIZE == 0) || \
    ((IOT_TRACE_BUFFER_SIZE & (IOT_TRACE_BUFFER_SIZE - 1)) != 0)
#error "IOT_TRACE_BUFFER_SIZE must be a power of two"
#endif

#define IOT_TRACE_MASK ((uint32_t)IOT_TRACE_BUFFER_SIZE - 1U)

static iot_trace_event_t s_ring[IOT_TRACE_BUFFER_SIZE];
static volatile uint32_t s_head;
static volatile uint32_t s_seq;
static volatile uint32_t s_dropped;
static volatile int      s_in_emit;
static volatile int      s_frozen;
static iot_trace_clock_fn s_clock;
static uint8_t            s_last_level = 0xFFU;

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

const char *iot_trace_event_name(uint16_t id)
{
    const char *const *table;
    uint32_t count;
    uint8_t seq = IOT_TRACE_SEQ(id);

    switch (IOT_TRACE_GROUP(id)) {
    case IOT_TRACE_GRP_CORE:
        table = s_core_names; count = sizeof(s_core_names) / sizeof(s_core_names[0]); break;
    case IOT_TRACE_GRP_OBJ:
        table = s_obj_names;  count = sizeof(s_obj_names) / sizeof(s_obj_names[0]);  break;
    case IOT_TRACE_GRP_ISR:
        table = s_isr_names;  count = sizeof(s_isr_names) / sizeof(s_isr_names[0]);  break;
    case IOT_TRACE_GRP_FAULT:
        table = s_fault_names; count = sizeof(s_fault_names) / sizeof(s_fault_names[0]); break;
    case IOT_TRACE_GRP_SYS:
        table = s_sys_names;  count = sizeof(s_sys_names) / sizeof(s_sys_names[0]);  break;
    default:
        return "unknown";
    }
    return (seq < count) ? table[seq] : "unknown";
}

/* ------------------------------ JSON 输出工具（零 libc 依赖） ------------------------------ */

static void json_put(char *buf, uint32_t max, uint32_t *pos, const char *s)
{
    while ((*s != '\0') && (*pos < max)) {
        buf[(*pos)++] = *s++;
    }
}

static void json_u32(char *buf, uint32_t max, uint32_t *pos, uint32_t v)
{
    char tmp[10];
    uint32_t n = 0U;

    if (v == 0U) {
        json_put(buf, max, pos, "0");
        return;
    }
    while (v > 0U) {
        tmp[n++] = (char)('0' + (v % 10U));
        v /= 10U;
    }
    while (n > 0U) {
        n--;
        if (*pos < max) {
            buf[(*pos)++] = tmp[n];
        }
    }
}

void iot_trace_init(void)
{
    uint32_t i;

    for (i = 0U; i < IOT_TRACE_BUFFER_SIZE; i++) {
        s_ring[i].id = 0U;
        s_ring[i].seq = 0U;
    }
    s_head = 0U;
    s_seq = 0U;
    s_dropped = 0U;
    s_in_emit = 0;
    s_frozen = 0;
    s_clock = NULL;
    s_last_level = 0xFFU;

    iot_trace_barectf_init();
    iot_trace_barectf_enable(g_iot_trace_level < IOT_TRACE_LEVEL_OFF);
    s_last_level = g_iot_trace_level;
}

/* 运行期等级变化时把 barectf 的内置开关同步过来 */
static void iot_trace_sync_backend(void)
{
    uint8_t level = g_iot_trace_level;

    if (level != s_last_level) {
        s_last_level = level;
        iot_trace_barectf_enable(level < IOT_TRACE_LEVEL_OFF);
    }
}

void iot_trace_emit(uint16_t evt, uint32_t v1, uint32_t v2)
{
    uint32_t ts;
    uint32_t slot;
    uint32_t seq;
    uint32_t task;
    volatile iot_trace_event_t *e;

    /* 重入/并发保护：只保留第一个进入者，其余计入 dropped */
    if (__sync_lock_test_and_set(&s_in_emit, 1) != 0) {
        (void)__sync_fetch_and_add(&s_dropped, 1U);
        return;
    }

    /* 崩溃后冻结快照，保留崩溃前最后一批事件 */
    if (s_frozen) {
        (void)__sync_fetch_and_add(&s_dropped, 1U);
        __sync_lock_release(&s_in_emit);
        return;
    }

    iot_trace_sync_backend();

    if (iot_port_in_isr()) {
        evt |= IOT_TRACE_FLAG_ISR;
    }

    ts = (s_clock != NULL) ? s_clock() : iot_port_trace_time_us();
    task = (uint32_t)(uintptr_t)iot_current_task;
    seq = __sync_fetch_and_add(&s_seq, 1U);
    slot = __sync_fetch_and_add(&s_head, 1U) & IOT_TRACE_MASK;

    e = &s_ring[slot];
    e->ts = ts;
    e->task = task;
    e->value = v1;
    e->arg = v2;
    e->id = evt;
    e->seq = (uint16_t)seq;      /* 最后写入，作为提交标志 */

    iot_trace_barectf_emit(evt, ts, task, v1, v2);

    __sync_lock_release(&s_in_emit);
}

void iot_trace_enable(void)
{
    iot_trace_set_level(IOT_TRACE_LEVEL_DEBUG);
    iot_trace_sync_backend();
}

void iot_trace_disable(void)
{
    iot_trace_set_level(IOT_TRACE_LEVEL_OFF);
    iot_trace_sync_backend();
}

void iot_trace_reset(void)
{
    uint32_t i;

    for (i = 0U; i < IOT_TRACE_BUFFER_SIZE; i++) {
        s_ring[i].id = 0U;
        s_ring[i].seq = 0U;
    }
    s_head = 0U;
    s_seq = 0U;
    s_dropped = 0U;
    s_last_level = 0xFFU;
}

void iot_trace_set_sink(iot_trace_sink_fn fn, void *user)
{
    iot_trace_barectf_set_sink(fn, user);
}

void iot_trace_set_clock(iot_trace_clock_fn fn)
{
    s_clock = fn;
}

int iot_trace_flush(void)
{
    return iot_trace_barectf_flush();
}

uint32_t iot_trace_count(void)
{
    return s_seq;
}

uint32_t iot_trace_dropped(void)
{
    return s_dropped;
}

uint32_t iot_trace_capacity(void)
{
    return (uint32_t)IOT_TRACE_BUFFER_SIZE;
}

const iot_trace_event_t *iot_trace_at(uint32_t index)
{
    if (index >= IOT_TRACE_BUFFER_SIZE) {
        return NULL;
    }
    return (const iot_trace_event_t *)&s_ring[index];
}

/* ------------------------------ 名称映射 ------------------------------ */

void iot_trace_name(const void *obj, uint8_t type, const char *name)
{
    uint32_t len = 0U;
    uint32_t i;

    if ((obj == NULL) || (name == NULL) ||
        (g_iot_trace_level > IOT_TRACE_LEVEL_CORE)) {
        return;
    }

    while ((name[len] != '\0') && (len < 64U)) {
        len++;
    }

    /* BEGIN: value=对象指针, arg=type | (名字长度 << 8) */
    iot_trace_emit(IOT_EVT_NAME_BEGIN, (uint32_t)(uintptr_t)obj,
                   (uint32_t)type | (len << 8));

    /* CHUNK: value=4 字节名字片段(小端), arg=分片序号 */
    for (i = 0U; i < len; i += 4U) {
        uint32_t chunk = 0U;
        uint32_t j;

        for (j = 0U; (j < 4U) && ((i + j) < len); j++) {
            chunk |= ((uint32_t)(uint8_t)name[i + j]) << (j * 8U);
        }
        iot_trace_emit(IOT_EVT_NAME_CHUNK, chunk, i / 4U);
    }
}

/* ------------------------------ 崩溃冻结 ------------------------------ */

void iot_trace_freeze(void)   { s_frozen = 1; }
void iot_trace_unfreeze(void) { s_frozen = 0; }
bool iot_trace_is_frozen(void) { return s_frozen != 0; }

/* ------------------------------ Perfetto / Chrome Trace JSON ------------------------------ */

int iot_trace_export_json(char *buf, uint32_t max)
{
    uint32_t pos = 0U;
    uint32_t n;
    uint32_t first;
    uint32_t seq;
    bool comma = false;

    if ((buf == NULL) || (max < 2U)) {
        return IOT_INVALID_PARAM;
    }

    json_put(buf, max, &pos, "[");
    n = s_seq;
    first = (n > IOT_TRACE_BUFFER_SIZE) ? (n - IOT_TRACE_BUFFER_SIZE) : 0U;

    for (seq = first; seq < n; seq++) {
        const iot_trace_event_t *e = &s_ring[seq & IOT_TRACE_MASK];

        if (e->id == 0U) {
            continue;
        }
        if (comma) {
            json_put(buf, max, &pos, ",");
        }
        comma = true;

        json_put(buf, max, &pos, "{\"name\":\"");
        json_put(buf, max, &pos, iot_trace_event_name(e->id));
        json_put(buf, max, &pos, "\",\"ph\":\"i\",\"s\":\"t\",\"ts\":");
        json_u32(buf, max, &pos, e->ts);
        json_put(buf, max, &pos, ",\"pid\":1,\"tid\":");
        json_u32(buf, max, &pos, e->task);
        json_put(buf, max, &pos, ",\"args\":{\"v\":");
        json_u32(buf, max, &pos, e->value);
        json_put(buf, max, &pos, ",\"a\":");
        json_u32(buf, max, &pos, e->arg);
        json_put(buf, max, &pos, ",\"isr\":");
        json_u32(buf, max, &pos, IOT_TRACE_IN_ISR(e->id) ? 1U : 0U);
        json_put(buf, max, &pos, "}}");
    }

    json_put(buf, max, &pos, "]");
    if (pos >= max) {
        buf[max - 1U] = '\0';
        return IOT_OVERFLOW;
    }
    buf[pos] = '\0';
    return (int)pos;
}

#else /* IOT_USE_TRACE */

/* 编译期裁剪：保留符号，保证引用方链接不失败 */
volatile uint8_t g_iot_trace_level = (uint8_t)IOT_TRACE_LEVEL_OFF;

void iot_trace_init(void) {}
void iot_trace_emit(uint16_t evt, uint32_t v1, uint32_t v2)
{
    (void)evt; (void)v1; (void)v2;
}
void iot_trace_enable(void) {}
void iot_trace_disable(void) {}
void iot_trace_reset(void) {}
void iot_trace_set_sink(iot_trace_sink_fn fn, void *user)
{
    (void)fn; (void)user;
}
void iot_trace_set_clock(iot_trace_clock_fn fn) { (void)fn; }
int  iot_trace_flush(void) { return IOT_NOT_READY; }
uint32_t iot_trace_count(void) { return 0U; }
uint32_t iot_trace_dropped(void) { return 0U; }
uint32_t iot_trace_capacity(void) { return 0U; }
const iot_trace_event_t *iot_trace_at(uint32_t index)
{
    (void)index;
    return NULL;
}
void iot_trace_name(const void *obj, uint8_t type, const char *name)
{
    (void)obj; (void)type; (void)name;
}
void iot_trace_freeze(void) {}
void iot_trace_unfreeze(void) {}
bool iot_trace_is_frozen(void) { return false; }
int iot_trace_export_json(char *buf, uint32_t max)
{
    if ((buf != NULL) && (max > 0U)) {
        buf[0] = '\0';
    }
    return IOT_NOT_READY;
}
const char *iot_trace_event_name(uint16_t id)
{
    (void)id;
    return "trace_off";
}

/* 后端桥接的桩统一由 iot_trace_barectf.c 提供，避免重复定义 */

#endif /* IOT_USE_TRACE */
