#ifndef ROS_CRASH_H
#define ROS_CRASH_H

/*
 * ros_crash.h - 崩溃现场记录（复位后仍保留，便于远程分析）
 *
 * 目标：异常（断言失败、HardFault、栈溢出、PC 致命信号）发生时，把
 *       “最小但足够定位”的现场写入一块复位不清零的 RAM（.noinit），
 *       下次启动时读取并通过 4G/MQTT/串口上报，避免只能依赖现场调试器。
 *
 * 记录内容：
 *   - 原因分类、崩溃序号、系统 tick、固件编译时间（便于匹配版本）；
 *   - 当前任务名/优先级/状态/栈余量；
 *   - Cortex-M 异常栈帧（R0-R3/R12/LR/PC/xPSR）与 CFSR/HFSR/MMFAR/BFAR/AFSR；
 *   - 断言文件/行号/表达式，或平台补充的错误码（信号号/异常码）；
 *   - 崩溃点附近的少量栈内存快照（仅当 SP 落在当前任务栈内才采集，
 *     防止读取非法地址导致二次异常）。
 *
 * 使用流程：
 *   ros_init();                        // 内部调用 ros_crash_init()：
 *                                      //   校验上次复位留下的记录并安装异常钩子
 *   if (ros_crash_pending()) {
 *       char buf[1280];
 *       ros_crash_format(ros_crash_get(), buf, sizeof(buf));
 *       upload(buf);                   // 通过 4G/MQTT/串口上报
 *       ros_crash_clear();             // 上报成功后清除，避免重复上报
 *   }
 *
 * 约束与移植要求：
 *   - 同一次启动内只保留第一次现场（后续异常不覆盖根因）；
 *   - Cortex-M：链接脚本需提供 NOLOAD 的 .noinit 段（本仓库 board 链接脚本已
 *     提供）；未提供时记录仍能生成，但不跨复位保留；
 *   - PC 模拟端：进程退出后记录消失，仅用于本地打印定位；
 *   - 断电会丢失记录（RAM 不掉电保存）；需要掉电保存时由应用把
 *     ros_crash_format() 的文本写入 Flash；
 *   - 记录中的寄存器为 32 位；PC 64 位进程的地址会被截断，仅作参考。
 */

#include "ros_config.h"
#include "ros_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 记录魔数与版本：用于识别 .noinit 中的随机数据 */
#define ROS_CRASH_MAGIC        0x43525348UL   /* "CRSH" */
#define ROS_CRASH_VERSION      1U

/* 各字段容量（含结尾 '\0'，超长截断） */
#define ROS_CRASH_TASK_MAX     16U
#define ROS_CRASH_FILE_MAX     32U
#define ROS_CRASH_EXPR_MAX     48U
#define ROS_CRASH_BUILD_MAX    24U

/* 栈快照字数：64 字 = 256 字节，可覆盖多层返回地址/局部变量 */
#ifndef ROS_CRASH_STACK_WORDS
#define ROS_CRASH_STACK_WORDS  64U
#endif

/* 崩溃原因 */
enum {
    ROS_CRASH_NONE = 0,
    ROS_CRASH_ASSERT,          /* 断言失败 */
    ROS_CRASH_HARD_FAULT,      /* Cortex-M HardFault */
    ROS_CRASH_MEMMANAGE,       /* Cortex-M MemManage */
    ROS_CRASH_BUSFAULT,        /* Cortex-M BusFault */
    ROS_CRASH_USAGEFAULT,      /* Cortex-M UsageFault */
    ROS_CRASH_STACK_OVERFLOW,  /* 任务栈溢出 */
    ROS_CRASH_SIGNAL,          /* PC 端致命信号/未处理异常 */
    ROS_CRASH_WATCHDOG,        /* 应用检测到看门狗复位等 */
    ROS_CRASH_USER             /* 应用自定义 */
};

/*
 * 采集上下文：由平台层或调用方填充，ros_crash_record() 会拷贝到持久记录。
 * 未采集的字段填 0 即可（建议 memset 清零）。
 */
typedef struct {
    uint32_t sp;               /* 崩溃时的栈指针（异常栈帧基址） */
    uint32_t regs[8];          /* R0,R1,R2,R3,R12,LR,PC,xPSR */
    uint32_t cfsr;             /* Cortex-M 可配置故障状态 */
    uint32_t hfsr;             /* Cortex-M 硬故障状态 */
    uint32_t mmfar;            /* MemManage 地址 */
    uint32_t bfar;             /* BusFault 地址 */
    uint32_t afsr;             /* 辅助故障状态（M4/M7） */
    uint32_t exc_return;       /* EXC_RETURN（判断异常使用 MSP 还是 PSP） */
    uint32_t code;             /* 平台错误码：信号号/异常码等 */
    const char *file;          /* 断言/错误位置，可为 NULL */
    uint32_t line;             /* 断言行号，可为 NULL 时忽略 */
    const char *expr;          /* 断言表达式或补充描述，可为 NULL */
} ros_crash_context_t;

/* 持久化记录（布局固定，CRC 校验保护） */
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t reason;
    uint32_t seq;              /* 崩溃序号，跨复位累加 */
    uint32_t tick;             /* 崩溃时的内核 tick */
    char build[ROS_CRASH_BUILD_MAX];  /* 固件编译时间 */

    char task[ROS_CRASH_TASK_MAX];    /* 崩溃时任务名 */
    uint16_t task_priority;
    uint16_t task_state;
    uint32_t task_stack_free;  /* 任务栈余量（字节，栈水位） */

    uint32_t sp;
    uint32_t regs[8];
    uint32_t cfsr;
    uint32_t hfsr;
    uint32_t mmfar;
    uint32_t bfar;
    uint32_t afsr;
    uint32_t exc_return;
    uint32_t code;

    uint32_t stack_sp;         /* 栈快照起始地址 */
    uint16_t stack_words;      /* 栈快照有效字数 */
    uint16_t reserved;
    uint32_t stack[ROS_CRASH_STACK_WORDS];

    char file[ROS_CRASH_FILE_MAX];
    uint32_t line;
    char expr[ROS_CRASH_EXPR_MAX];

    uint32_t crc;              /* 前面所有字节的 CRC32 */
} ros_crash_record_t;

/*
 * 初始化：安装异常捕获钩子，并校验 .noinit 中的上次记录。
 * 由 ros_init() 首先调用；重复调用会把“本次启动已记录”标志清零。
 * 若存在上次崩溃记录，会直接打印一份完整报告（便于串口/调试器查看）。
 */
void ros_crash_init(void);

/* 是否有上次启动遗留的崩溃记录（1 = 有，0 = 无） */
int ros_crash_pending(void);

/* 读取上次崩溃记录；无有效记录返回 NULL */
const ros_crash_record_t *ros_crash_get(void);

/* 清除记录（上报成功后调用）；同时允许本次启动重新记录 */
void ros_crash_clear(void);

/*
 * 记录一次崩溃现场。
 * 参数：reason - ROS_CRASH_* 原因；ctx - 采集上下文，可为 NULL（只记录任务/tick）。
 * 返回：0 成功；-1 原因非法。
 * 说明：可在中断/异常上下文调用；同一次启动内重复调用只保留第一次现场。
 */
int ros_crash_record(int reason, const ros_crash_context_t *ctx);

/*
 * 把记录格式化为可上报的文本（多行，行尾 "\r\n"）。
 * 参数：rec - 记录；out - 输出缓冲；max - 缓冲字节数。
 * 返回：写入的字符数（不含 '\0'）；参数非法返回 -1；空间不足时截断。
 */
int ros_crash_format(const ros_crash_record_t *rec, char *out, uint32_t max);

/* 把记录打印到 stdout/日志串口（不依赖日志等级，始终输出） */
void ros_crash_print(const ros_crash_record_t *rec);

/* 原因枚举 -> 短名称（未知返回 "UNKNOWN"） */
const char *ros_crash_reason_name(int reason);

/*
 * 平台钩子：安装异常捕获（PC 信号 / Cortex-M 故障处理）。
 * 由 ros_crash_init() 调用，普通应用无需直接调用。
 */
void ros_crash_port_install(void);

#ifdef __cplusplus
}
#endif

#endif /* ROS_CRASH_H */
