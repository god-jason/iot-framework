#ifndef ROS_TRACE_H
#define ROS_TRACE_H

/*
 * ros_trace.h - RTOS 事件跟踪（宏 + 运行期动态开关 + 流式输出）
 *
 * 形态与 log.h 的 LOG_D 一致：一个全局运行期等级 g_ros_trace_level，
 * 宏先比较等级，再决定是否调用 ros_trace_emit()。
 * 关闭时只有一次 volatile 读 + 一次分支。
 *
 *   ROS_TRACE_CORE(evt, v1, v2)   调度/任务/tick 等核心事件
 *   ROS_TRACE_OBJ (evt, v1, v2)   内核对象操作
 *   ROS_TRACE_DBG (evt, v1, v2)   细粒度调试事件
 *   ROS_TRACE     (evt, v1, v2)   CORE 的简写
 *
 * 输出模型（无内存快照、无环形缓冲）：
 *   事件产生即格式化为一行文本并交给平台的 ros_port_trace_write()：
 *     T <ts_us> <evt_name> task=0x<指针> v1=<n> v2=<n> [isr=1]
 *   PC 平台写入文件，MCU 平台由 ros_trace_set_output() 指定的串口发出。
 *
 * 运行期开关（宏，直接读写全局变量，与 log_set_level 同风格）：
 *   ros_trace_set_level(ROS_TRACE_LEVEL_OFF);
 *   ros_trace_enable();  ros_trace_disable();
 */

#include "ros_config.h"
#include "ros_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================== 运行期等级 ============================== */

/*
 * 与 g_log_level 完全同向：数值越小记录越多。
 * 宏的条件是 g_ros_trace_level <= 该事件的等级，因此
 * DEBUG(0) 时三类宏全开，CORE(2) 时只剩 CORE，OFF(3) 时全关。
 */
#define ROS_TRACE_LEVEL_DEBUG  0U   /* 全部事件（= enable） */
#define ROS_TRACE_LEVEL_OBJ    1U   /* 核心 + 内核对象操作 */
#define ROS_TRACE_LEVEL_CORE   2U   /* 仅核心调度事件 */
#define ROS_TRACE_LEVEL_OFF    3U   /* 关闭 */

#ifndef ROS_TRACE_DEFAULT_LEVEL
#define ROS_TRACE_DEFAULT_LEVEL ROS_TRACE_LEVEL_OFF
#endif

extern volatile uint8_t g_ros_trace_level;

#define ros_trace_set_level(level) ((void)(g_ros_trace_level = (uint8_t)(level)))
#define ros_trace_get_level()      ((uint8_t)g_ros_trace_level)

/* ============================== 事件 ID ============================== */

#define ROS_TRACE_GRP_CORE   1U
#define ROS_TRACE_GRP_OBJ    2U
#define ROS_TRACE_GRP_ISR    3U
#define ROS_TRACE_GRP_FAULT  4U
#define ROS_TRACE_GRP_SYS    5U

#define ROS_TRACE_EVT(group, n) \
    ((uint16_t)(((uint16_t)(group) << 8) | (uint8_t)(n)))

#define ROS_TRACE_GROUP(id) ((uint8_t)(((uint16_t)(id)) >> 8))

/* ISR 标记放在组内序号 bit7：组内事件最多 15 个，bit7 空闲 */
#define ROS_TRACE_FLAG_ISR   0x0080U
#define ROS_TRACE_SEQ(id)    ((uint8_t)(((uint16_t)(id)) & 0x7FU))
#define ROS_TRACE_IN_ISR(id) ((((uint16_t)(id)) & ROS_TRACE_FLAG_ISR) != 0U)

enum {
    /* CORE：调度与任务 */
    ROS_EVT_SWITCH         = ROS_TRACE_EVT(ROS_TRACE_GRP_CORE, 0),
    ROS_EVT_TASK_READY     = ROS_TRACE_EVT(ROS_TRACE_GRP_CORE, 1),
    ROS_EVT_TASK_BLOCK     = ROS_TRACE_EVT(ROS_TRACE_GRP_CORE, 2),
    ROS_EVT_TASK_WAKE      = ROS_TRACE_EVT(ROS_TRACE_GRP_CORE, 3),
    ROS_EVT_TASK_TIMEOUT   = ROS_TRACE_EVT(ROS_TRACE_GRP_CORE, 4),
    ROS_EVT_TASK_CREATE    = ROS_TRACE_EVT(ROS_TRACE_GRP_CORE, 5),
    ROS_EVT_TASK_DESTROY   = ROS_TRACE_EVT(ROS_TRACE_GRP_CORE, 6),
    ROS_EVT_TASK_EXIT      = ROS_TRACE_EVT(ROS_TRACE_GRP_CORE, 7),
    ROS_EVT_TASK_SLEEP     = ROS_TRACE_EVT(ROS_TRACE_GRP_CORE, 8),
    ROS_EVT_PRIO_CHANGE    = ROS_TRACE_EVT(ROS_TRACE_GRP_CORE, 9),
    ROS_EVT_TICK           = ROS_TRACE_EVT(ROS_TRACE_GRP_CORE, 10),
    ROS_EVT_SLICE_EXPIRE   = ROS_TRACE_EVT(ROS_TRACE_GRP_CORE, 11),
    ROS_EVT_PREEMPT        = ROS_TRACE_EVT(ROS_TRACE_GRP_CORE, 12),
    ROS_EVT_IDLE_ENTER     = ROS_TRACE_EVT(ROS_TRACE_GRP_CORE, 13),
    ROS_EVT_IDLE_EXIT      = ROS_TRACE_EVT(ROS_TRACE_GRP_CORE, 14),
    ROS_EVT_TASK_SUSPEND   = ROS_TRACE_EVT(ROS_TRACE_GRP_CORE, 15),
    ROS_EVT_TASK_RESUME    = ROS_TRACE_EVT(ROS_TRACE_GRP_CORE, 16),
    ROS_EVT_TICKLESS_ENTER = ROS_TRACE_EVT(ROS_TRACE_GRP_CORE, 17),
    ROS_EVT_TICKLESS_EXIT  = ROS_TRACE_EVT(ROS_TRACE_GRP_CORE, 18),

    /* OBJ：内核对象操作 */
    ROS_EVT_SEM_TAKE       = ROS_TRACE_EVT(ROS_TRACE_GRP_OBJ, 0),
    ROS_EVT_SEM_GIVE       = ROS_TRACE_EVT(ROS_TRACE_GRP_OBJ, 1),
    ROS_EVT_MUTEX_LOCK     = ROS_TRACE_EVT(ROS_TRACE_GRP_OBJ, 2),
    ROS_EVT_MUTEX_UNLOCK   = ROS_TRACE_EVT(ROS_TRACE_GRP_OBJ, 3),
    ROS_EVT_QUEUE_SEND     = ROS_TRACE_EVT(ROS_TRACE_GRP_OBJ, 4),
    ROS_EVT_QUEUE_RECV     = ROS_TRACE_EVT(ROS_TRACE_GRP_OBJ, 5),
    ROS_EVT_MSGQ_SEND      = ROS_TRACE_EVT(ROS_TRACE_GRP_OBJ, 6),
    ROS_EVT_MSGQ_RECV      = ROS_TRACE_EVT(ROS_TRACE_GRP_OBJ, 7),
    ROS_EVT_SBUF_WRITE     = ROS_TRACE_EVT(ROS_TRACE_GRP_OBJ, 8),
    ROS_EVT_SBUF_READ      = ROS_TRACE_EVT(ROS_TRACE_GRP_OBJ, 9),
    ROS_EVT_EVENT_SET      = ROS_TRACE_EVT(ROS_TRACE_GRP_OBJ, 10),
    ROS_EVT_EVENT_WAIT     = ROS_TRACE_EVT(ROS_TRACE_GRP_OBJ, 11),
    ROS_EVT_NOTIFY         = ROS_TRACE_EVT(ROS_TRACE_GRP_OBJ, 12),
    ROS_EVT_TIMER_FIRE     = ROS_TRACE_EVT(ROS_TRACE_GRP_OBJ, 13),

    /* ISR：中断相关 */
    ROS_EVT_ISR_ENTER      = ROS_TRACE_EVT(ROS_TRACE_GRP_ISR, 0),
    ROS_EVT_ISR_EXIT       = ROS_TRACE_EVT(ROS_TRACE_GRP_ISR, 1),
    ROS_EVT_FROM_ISR       = ROS_TRACE_EVT(ROS_TRACE_GRP_ISR, 2),

    /* FAULT：崩溃 / 断言 / 栈溢出 */
    ROS_EVT_ASSERT         = ROS_TRACE_EVT(ROS_TRACE_GRP_FAULT, 0),
    ROS_EVT_CRASH          = ROS_TRACE_EVT(ROS_TRACE_GRP_FAULT, 1),
    ROS_EVT_STACK_OVERFLOW = ROS_TRACE_EVT(ROS_TRACE_GRP_FAULT, 2),

    /* SYS：trace 自身 */
    ROS_EVT_MARKER         = ROS_TRACE_EVT(ROS_TRACE_GRP_SYS, 0),
    ROS_EVT_TRACE_OVERFLOW = ROS_TRACE_EVT(ROS_TRACE_GRP_SYS, 1),
    ROS_EVT_TIME_SYNC      = ROS_TRACE_EVT(ROS_TRACE_GRP_SYS, 2),
    ROS_EVT_NAME_BEGIN     = ROS_TRACE_EVT(ROS_TRACE_GRP_SYS, 3),
    ROS_EVT_NAME_CHUNK     = ROS_TRACE_EVT(ROS_TRACE_GRP_SYS, 4)
};

/* ============================== 宏 ============================== */

#if ROS_USE_TRACE

#define ROS_TRACE_L(level, evt, v1, v2)                                  \
    do {                                                                   \
        if (g_ros_trace_level <= (uint8_t)(level)) {                       \
            ros_trace_emit((uint16_t)(evt), (uint32_t)(v1), (uint32_t)(v2)); \
        }                                                                  \
    } while (0)

#else

#define ROS_TRACE_L(level, evt, v1, v2) ((void)0)

#endif

#define ROS_TRACE_CORE(evt, v1, v2) ROS_TRACE_L(ROS_TRACE_LEVEL_CORE,  evt, v1, v2)
#define ROS_TRACE_OBJ(evt, v1, v2)  ROS_TRACE_L(ROS_TRACE_LEVEL_OBJ,   evt, v1, v2)
#define ROS_TRACE_DBG(evt, v1, v2)  ROS_TRACE_L(ROS_TRACE_LEVEL_DEBUG, evt, v1, v2)
#define ROS_TRACE(evt, v1, v2)      ROS_TRACE_CORE(evt, v1, v2)

/* ============================== API ============================== */

typedef uint32_t (*ros_trace_clock_fn)(void);

void     ros_trace_init(void);
void     ros_trace_emit(uint16_t evt, uint32_t v1, uint32_t v2);
void     ros_trace_enable(void);
void     ros_trace_disable(void);
void     ros_trace_reset(void);       /* 复位事件计数 */
void     ros_trace_set_clock(ros_trace_clock_fn fn);

/* 名称映射：直接输出 NAME_BEGIN / NAME_CHUNK 事件行 */
#define ROS_TRACE_NAME_TASK  1U
#define ROS_TRACE_NAME_OBJ   2U
void ros_trace_name(const void *obj, uint8_t type, const char *name);

/* 崩溃冻结：冻结后不再发射（此时事件已全部流式输出，无需保留快照） */
void ros_trace_freeze(void);
void ros_trace_unfreeze(void);
bool ros_trace_is_frozen(void);

const char *ros_trace_event_name(uint16_t id);
uint32_t    ros_trace_count(void);    /* 已输出事件数 */
uint32_t    ros_trace_dropped(void);  /* 因重入/冻结丢弃的事件数 */

/*
 * 注册 trace 输出通道（平台解释 handle 的含义）：
 *   - MCU：handle 为已初始化的 hal_uart_t *，timeout_ms 为发送超时；
 *   - PC ：handle 忽略，输出到 ros_trace_start_file() 指定的文件。
 */
void ros_trace_set_output(void *handle, uint32_t timeout_ms);

/*
 * 文本行流输出（PC 写入文件；不支持文件系统的平台为空实现）。
 * 未调用 start_file 时 PC 端默认写入 "ros_trace.log"；stop_file 关闭文件。
 */
void ros_trace_start_file(const char *path);
void ros_trace_stop_file(void);

/* 平台输出钩子：由各平台目录下的 trace 实现文件提供 */
void ros_port_trace_write(const char *data, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* ROS_TRACE_H */
