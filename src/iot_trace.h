#ifndef IOT_TRACE_H
#define IOT_TRACE_H

/*
 * iot_trace.h - RTOS 事件跟踪（宏 + 运行期动态开关 + 流式输出）
 *
 * 形态与 log.h 的 LOG_D 一致：一个全局运行期等级 g_iot_trace_level，
 * 宏先比较等级，再决定是否调用 iot_trace_emit()。
 * 关闭时只有一次 volatile 读 + 一次分支。
 *
 *   IOT_TRACE_CORE(evt, v1, v2)   调度/任务/tick 等核心事件
 *   IOT_TRACE_OBJ (evt, v1, v2)   内核对象操作
 *   IOT_TRACE_DBG (evt, v1, v2)   细粒度调试事件
 *   IOT_TRACE     (evt, v1, v2)   CORE 的简写
 *
 * 输出模型（无内存快照、无环形缓冲）：
 *   事件产生即格式化为一行文本并交给平台的 iot_port_trace_write()：
 *     T <ts_us> <evt_name> task=0x<指针> v1=<n> v2=<n> [isr=1]
 *   PC 平台写入文件，MCU 平台由 iot_trace_set_output() 指定的串口发出。
 *
 * 运行期开关（宏，直接读写全局变量，与 log_set_level 同风格）：
 *   iot_trace_set_level(IOT_TRACE_LEVEL_OFF);
 *   iot_trace_enable();  iot_trace_disable();
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

#ifndef IOT_TRACE_DEFAULT_LEVEL
#define IOT_TRACE_DEFAULT_LEVEL IOT_TRACE_LEVEL_OFF
#endif

extern volatile uint8_t g_iot_trace_level;

#define iot_trace_set_level(level) ((void)(g_iot_trace_level = (uint8_t)(level)))
#define iot_trace_get_level()      ((uint8_t)g_iot_trace_level)

/* ============================== 事件 ID ============================== */

#define IOT_TRACE_GRP_CORE   1U
#define IOT_TRACE_GRP_OBJ    2U
#define IOT_TRACE_GRP_ISR    3U
#define IOT_TRACE_GRP_FAULT  4U
#define IOT_TRACE_GRP_SYS    5U

#define IOT_TRACE_EVT(group, n) \
    ((uint16_t)(((uint16_t)(group) << 8) | (uint8_t)(n)))

#define IOT_TRACE_GROUP(id) ((uint8_t)(((uint16_t)(id)) >> 8))

/* ISR 标记放在组内序号 bit7：组内事件最多 15 个，bit7 空闲 */
#define IOT_TRACE_FLAG_ISR   0x0080U
#define IOT_TRACE_SEQ(id)    ((uint8_t)(((uint16_t)(id)) & 0x7FU))
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

#define IOT_TRACE_L(level, evt, v1, v2)                                  \
    do {                                                                   \
        if (g_iot_trace_level <= (uint8_t)(level)) {                       \
            iot_trace_emit((uint16_t)(evt), (uint32_t)(v1), (uint32_t)(v2)); \
        }                                                                  \
    } while (0)

#else

#define IOT_TRACE_L(level, evt, v1, v2) ((void)0)

#endif

#define IOT_TRACE_CORE(evt, v1, v2) IOT_TRACE_L(IOT_TRACE_LEVEL_CORE,  evt, v1, v2)
#define IOT_TRACE_OBJ(evt, v1, v2)  IOT_TRACE_L(IOT_TRACE_LEVEL_OBJ,   evt, v1, v2)
#define IOT_TRACE_DBG(evt, v1, v2)  IOT_TRACE_L(IOT_TRACE_LEVEL_DEBUG, evt, v1, v2)
#define IOT_TRACE(evt, v1, v2)      IOT_TRACE_CORE(evt, v1, v2)

/* ============================== API ============================== */

typedef uint32_t (*iot_trace_clock_fn)(void);

void     iot_trace_init(void);
void     iot_trace_emit(uint16_t evt, uint32_t v1, uint32_t v2);
void     iot_trace_enable(void);
void     iot_trace_disable(void);
void     iot_trace_reset(void);       /* 复位事件计数 */
void     iot_trace_set_clock(iot_trace_clock_fn fn);

/* 名称映射：直接输出 NAME_BEGIN / NAME_CHUNK 事件行 */
#define IOT_TRACE_NAME_TASK  1U
#define IOT_TRACE_NAME_OBJ   2U
void iot_trace_name(const void *obj, uint8_t type, const char *name);

/* 崩溃冻结：冻结后不再发射（此时事件已全部流式输出，无需保留快照） */
void iot_trace_freeze(void);
void iot_trace_unfreeze(void);
bool iot_trace_is_frozen(void);

const char *iot_trace_event_name(uint16_t id);
uint32_t    iot_trace_count(void);    /* 已输出事件数 */
uint32_t    iot_trace_dropped(void);  /* 因重入/冻结丢弃的事件数 */

/*
 * 注册 trace 输出通道（平台解释 handle 的含义）：
 *   - MCU：handle 为已初始化的 hal_uart_t *，timeout_ms 为发送超时；
 *   - PC ：handle 忽略，输出到 iot_trace_start_file() 指定的文件。
 */
void iot_trace_set_output(void *handle, uint32_t timeout_ms);

/*
 * 文本行流输出（PC 写入文件；不支持文件系统的平台为空实现）。
 * 未调用 start_file 时 PC 端默认写入 "iot_trace.log"；stop_file 关闭文件。
 */
void iot_trace_start_file(const char *path);
void iot_trace_stop_file(void);

/* 平台输出钩子：由各平台目录下的 trace 实现文件提供 */
void iot_port_trace_write(const char *data, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* IOT_TRACE_H */
