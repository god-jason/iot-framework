#ifndef IOT_TRACE_H
#define IOT_TRACE_H

/*
 * iot_trace.h - RTOS 事件跟踪（宏 + 运行期动态开关）
 *
 * 形态与 log.h 的 LOG_D 一致：一个全局运行期等级 g_iot_trace_level，
 * 宏先比较等级，再决定是否调用 iot_trace_emit()。
 * 因此关闭时只有一次 volatile 读 + 一次分支，不做任何缓冲/后端操作。
 *
 *   IOT_TRACE_CORE(evt, v1, v2)   调度/任务/tick 等核心事件
 *   IOT_TRACE_OBJ (evt, v1, v2)   内核对象操作
 *   IOT_TRACE_DBG (evt, v1, v2)   细粒度调试事件
 *   IOT_TRACE     (evt, v1, v2)   CORE 的简写
 *
 * 运行期开关（宏，直接读写全局变量，与 log_set_level 同风格）：
 *   iot_trace_set_level(IOT_TRACE_LEVEL_OFF);
 *   iot_trace_enable();  iot_trace_disable();
 *
 * 输出后端：
 *   - 内置环形快照（始终可用，供测试与事故回溯）；
 *   - barectf（IOT_TRACE_BACKEND_BARECTF=1 时），产出标准 CTF 二进制流，
 *     可用 babeltrace2 / Trace Compass 读取；由 sink 回调导出。
 */

#include "iot_config.h"
#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================== 运行期等级 ============================== */

/*
 * 与 g_log_level 完全同向：数值越小记录越多。
 * 宏的条件是 g_iot_trace_level <= 该事件的等级，因此
 * DEBUG(0) 时三类宏全开，CORE(2) 时只剩 CORE，OFF(3) 时全关。
 */
#define IOT_TRACE_LEVEL_DEBUG  0U   /* 全部事件（= enable） */
#define IOT_TRACE_LEVEL_OBJ    1U   /* 核心 + 内核对象操作 */
#define IOT_TRACE_LEVEL_CORE   2U   /* 仅核心调度事件 */
#define IOT_TRACE_LEVEL_OFF    3U   /* 关闭 */

/* 默认关闭：编译进代码但运行期不产生任何事件 */
#ifndef IOT_TRACE_DEFAULT_LEVEL
#define IOT_TRACE_DEFAULT_LEVEL IOT_TRACE_LEVEL_OFF
#endif

/* 全局运行期等级（定义在 iot_trace.c），volatile 保证 ISR 中可见 */
extern volatile uint8_t g_iot_trace_level;

#define iot_trace_set_level(level) ((void)(g_iot_trace_level = (uint8_t)(level)))
#define iot_trace_get_level()      ((uint8_t)g_iot_trace_level)

/* ============================== 事件 ID ============================== */

/* 高 8 位 = 事件组，低 8 位 = 组内序号，便于后端按组路由 CTF 记录类型 */
#define IOT_TRACE_GRP_CORE   1U
#define IOT_TRACE_GRP_OBJ    2U
#define IOT_TRACE_GRP_ISR    3U
#define IOT_TRACE_GRP_FAULT  4U
#define IOT_TRACE_GRP_SYS    5U

#define IOT_TRACE_EVT(group, n) \
    ((uint16_t)(((uint16_t)(group) << 8) | (uint8_t)(n)))

#define IOT_TRACE_GROUP(id) ((uint8_t)(((uint16_t)(id)) >> 8))

/*
 * ISR 标记放在「组内序号」的 bit7：组内事件最多 15 个，bit7 空闲。
 * 这样高 8 位的分组路由完全不变，CTF 的 op/generic.id 字段也自然带上标记，
 * 无需修改 barectf 配置与后端函数签名。
 */
#define IOT_TRACE_FLAG_ISR  0x0080U
#define IOT_TRACE_SEQ(id)   ((uint8_t)(((uint16_t)(id)) & 0x7FU))
#define IOT_TRACE_IN_ISR(id) ((((uint16_t)(id)) & IOT_TRACE_FLAG_ISR) != 0U)

enum {
    /* CORE：调度与任务 */
    IOT_EVT_SWITCH         = IOT_TRACE_EVT(IOT_TRACE_GRP_CORE, 0),
    IOT_EVT_TASK_READY     = IOT_TRACE_EVT(IOT_TRACE_GRP_CORE, 1),
    IOT_EVT_TASK_BLOCK     = IOT_TRACE_EVT(IOT_TRACE_GRP_CORE, 2),
    IOT_EVT_TASK_WAKE      = IOT_TRACE_EVT(IOT_TRACE_GRP_CORE, 3),
    IOT_EVT_TASK_TIMEOUT   = IOT_TRACE_EVT(IOT_TRACE_GRP_CORE, 4),
    IOT_EVT_TASK_CREATE    = IOT_TRACE_EVT(IOT_TRACE_GRP_CORE, 5),
    IOT_EVT_TASK_DESTROY   = IOT_TRACE_EVT(IOT_TRACE_GRP_CORE, 6),
    IOT_EVT_TASK_EXIT      = IOT_TRACE_EVT(IOT_TRACE_GRP_CORE, 7),
    IOT_EVT_TASK_SLEEP     = IOT_TRACE_EVT(IOT_TRACE_GRP_CORE, 8),
    IOT_EVT_PRIO_CHANGE    = IOT_TRACE_EVT(IOT_TRACE_GRP_CORE, 9),
    IOT_EVT_TICK           = IOT_TRACE_EVT(IOT_TRACE_GRP_CORE, 10),
    IOT_EVT_SLICE_EXPIRE   = IOT_TRACE_EVT(IOT_TRACE_GRP_CORE, 11),
    IOT_EVT_PREEMPT        = IOT_TRACE_EVT(IOT_TRACE_GRP_CORE, 12),
    IOT_EVT_IDLE_ENTER     = IOT_TRACE_EVT(IOT_TRACE_GRP_CORE, 13),
    IOT_EVT_IDLE_EXIT      = IOT_TRACE_EVT(IOT_TRACE_GRP_CORE, 14),
    IOT_EVT_TASK_SUSPEND   = IOT_TRACE_EVT(IOT_TRACE_GRP_CORE, 15),
    IOT_EVT_TASK_RESUME    = IOT_TRACE_EVT(IOT_TRACE_GRP_CORE, 16),
    IOT_EVT_TICKLESS_ENTER = IOT_TRACE_EVT(IOT_TRACE_GRP_CORE, 17),
    IOT_EVT_TICKLESS_EXIT  = IOT_TRACE_EVT(IOT_TRACE_GRP_CORE, 18),

    /* OBJ：内核对象操作 */
    IOT_EVT_SEM_TAKE       = IOT_TRACE_EVT(IOT_TRACE_GRP_OBJ, 0),
    IOT_EVT_SEM_GIVE       = IOT_TRACE_EVT(IOT_TRACE_GRP_OBJ, 1),
    IOT_EVT_MUTEX_LOCK     = IOT_TRACE_EVT(IOT_TRACE_GRP_OBJ, 2),
    IOT_EVT_MUTEX_UNLOCK   = IOT_TRACE_EVT(IOT_TRACE_GRP_OBJ, 3),
    IOT_EVT_QUEUE_SEND     = IOT_TRACE_EVT(IOT_TRACE_GRP_OBJ, 4),
    IOT_EVT_QUEUE_RECV     = IOT_TRACE_EVT(IOT_TRACE_GRP_OBJ, 5),
    IOT_EVT_MSGQ_SEND      = IOT_TRACE_EVT(IOT_TRACE_GRP_OBJ, 6),
    IOT_EVT_MSGQ_RECV      = IOT_TRACE_EVT(IOT_TRACE_GRP_OBJ, 7),
    IOT_EVT_SBUF_WRITE     = IOT_TRACE_EVT(IOT_TRACE_GRP_OBJ, 8),
    IOT_EVT_SBUF_READ      = IOT_TRACE_EVT(IOT_TRACE_GRP_OBJ, 9),
    IOT_EVT_EVENT_SET      = IOT_TRACE_EVT(IOT_TRACE_GRP_OBJ, 10),
    IOT_EVT_EVENT_WAIT     = IOT_TRACE_EVT(IOT_TRACE_GRP_OBJ, 11),
    IOT_EVT_NOTIFY         = IOT_TRACE_EVT(IOT_TRACE_GRP_OBJ, 12),
    IOT_EVT_TIMER_FIRE     = IOT_TRACE_EVT(IOT_TRACE_GRP_OBJ, 13),

    /* ISR：中断相关 */
    IOT_EVT_ISR_ENTER      = IOT_TRACE_EVT(IOT_TRACE_GRP_ISR, 0),
    IOT_EVT_ISR_EXIT       = IOT_TRACE_EVT(IOT_TRACE_GRP_ISR, 1),
    IOT_EVT_FROM_ISR       = IOT_TRACE_EVT(IOT_TRACE_GRP_ISR, 2),

    /* FAULT：崩溃 / 断言 / 栈溢出 */
    IOT_EVT_ASSERT         = IOT_TRACE_EVT(IOT_TRACE_GRP_FAULT, 0),
    IOT_EVT_CRASH          = IOT_TRACE_EVT(IOT_TRACE_GRP_FAULT, 1),
    IOT_EVT_STACK_OVERFLOW = IOT_TRACE_EVT(IOT_TRACE_GRP_FAULT, 2),

    /* SYS：trace 自身 */
    IOT_EVT_MARKER         = IOT_TRACE_EVT(IOT_TRACE_GRP_SYS, 0),
    IOT_EVT_TRACE_OVERFLOW = IOT_TRACE_EVT(IOT_TRACE_GRP_SYS, 1),
    IOT_EVT_TIME_SYNC      = IOT_TRACE_EVT(IOT_TRACE_GRP_SYS, 2),
    IOT_EVT_NAME_BEGIN     = IOT_TRACE_EVT(IOT_TRACE_GRP_SYS, 3),
    IOT_EVT_NAME_CHUNK     = IOT_TRACE_EVT(IOT_TRACE_GRP_SYS, 4)
};

/* ============================== 宏 ============================== */

#if IOT_USE_TRACE

/*
 * 与 LOG_D 完全相同的形态：先读全局运行期等级，再决定是否发射。
 * 参数在关闭时不会被求值，因此可以在事件参数里放函数调用而无开销。
 */
#define IOT_TRACE_L(level, evt, v1, v2)                                  \
    do {                                                                   \
        if (g_iot_trace_level <= (uint8_t)(level)) {                       \
            iot_trace_emit((uint16_t)(evt), (uint32_t)(v1), (uint32_t)(v2)); \
        }                                                                  \
    } while (0)

#else /* IOT_USE_TRACE */

#define IOT_TRACE_L(level, evt, v1, v2) ((void)0)

#endif /* IOT_USE_TRACE */

#define IOT_TRACE_CORE(evt, v1, v2) IOT_TRACE_L(IOT_TRACE_LEVEL_CORE,  evt, v1, v2)
#define IOT_TRACE_OBJ(evt, v1, v2)  IOT_TRACE_L(IOT_TRACE_LEVEL_OBJ,   evt, v1, v2)
#define IOT_TRACE_DBG(evt, v1, v2)  IOT_TRACE_L(IOT_TRACE_LEVEL_DEBUG, evt, v1, v2)
#define IOT_TRACE(evt, v1, v2)      IOT_TRACE_CORE(evt, v1, v2)

/* ============================== 数据结构与 API ============================== */

typedef struct iot_trace_event {
    uint32_t ts;        /* 微秒时间戳 */
    uint32_t task;      /* 当前任务对象指针的低 32 位 */
    uint32_t value;     /* 主载荷 */
    uint32_t arg;       /* 副载荷 */
    uint16_t id;        /* 事件 ID */
    uint16_t seq;       /* 包序号（最后写入，作为提交标志） */
} iot_trace_event_t;

/* CTF 包/快照导出回调：必须非阻塞，可在任意上下文调用 */
typedef void (*iot_trace_sink_fn)(const uint8_t *data, uint32_t len, void *user);

/* 时间戳回调（µs，单调）；不设置时默认用 iot_tick_get() * 1000 */
typedef uint32_t (*iot_trace_clock_fn)(void);

void     iot_trace_init(void);
void     iot_trace_emit(uint16_t evt, uint32_t v1, uint32_t v2);
void     iot_trace_enable(void);      /* = set_level(DEBUG) */
void     iot_trace_disable(void);     /* = set_level(OFF) */
void     iot_trace_reset(void);       /* 清空快照环与计数 */

void     iot_trace_set_sink(iot_trace_sink_fn fn, void *user);
void     iot_trace_set_clock(iot_trace_clock_fn fn);
int      iot_trace_flush(void);       /* 关闭当前 CTF packet 并交给 sink */

/* ---- 名称映射：把对象指针与可读名字写进事件流，供上位机还原泳道 ---- */
#define IOT_TRACE_NAME_TASK  1U
#define IOT_TRACE_NAME_OBJ   2U
void iot_trace_name(const void *obj, uint8_t type, const char *name);

/* ---- 崩溃冻结：保留崩溃前最后一批事件，不再被后续写入覆盖 ---- */
void iot_trace_freeze(void);
void iot_trace_unfreeze(void);
bool iot_trace_is_frozen(void);

/* ---- 主机侧导出：Chrome/Perfetto Trace Event JSON（ts 单位 µs） ---- */
int         iot_trace_export_json(char *buf, uint32_t max);
const char *iot_trace_event_name(uint16_t id);

#if defined(IOT_PLATFORM_STM32)
/* MCU 流式 sink：ITM/SWO stimulus port 0，非阻塞（FIFO 忙则丢弃） */
void iot_trace_sink_itm_init(void);
#endif

#if defined(IOT_PLATFORM_PC)
/* 便捷文件 sink：把 CTF 二进制流写入 path，与 vendor/barectf/metadata 同目录即可解析 */
void iot_trace_start_file(const char *path);
void iot_trace_stop_file(void);

/* 把环形快照导出为 Chrome/Perfetto Trace Event JSON 文件，浏览器直接打开 */
int iot_trace_dump_json_file(const char *path);
#endif

uint32_t iot_trace_count(void);       /* 已发射事件总数 */
uint32_t iot_trace_dropped(void);     /* 因重入/并发丢弃的事件数 */
uint32_t iot_trace_capacity(void);
const iot_trace_event_t *iot_trace_at(uint32_t index); /* index < capacity */

/* 后端桥接（iot_trace_barectf.c 实现，其它文件无需直接调用） */
void iot_trace_barectf_init(void);
void iot_trace_barectf_enable(int enable);
void iot_trace_barectf_set_sink(iot_trace_sink_fn fn, void *user);
void iot_trace_barectf_emit(uint16_t evt, uint32_t ts, uint32_t task,
                            uint32_t v1, uint32_t v2);
int  iot_trace_barectf_flush(void);

#ifdef __cplusplus
}
#endif

#endif /* IOT_TRACE_H */
