#ifndef SHELL_CONFIG_H
#define SHELL_CONFIG_H

/*
 * shell_config.h - IoT Shell V1.1 编译期配置
 *
 * 所有资源上限集中在此，资源受限工程可按需裁剪。
 */

/* ------------------------------ 缓冲区 ------------------------------ */
#ifndef SHELL_RX_MAX
#define SHELL_RX_MAX            128U    /* 输入环形缓冲（堆分配） */
#endif
#ifndef SHELL_LINE_MAX
#define SHELL_LINE_MAX          128U    /* 单行最大字节数 */
#endif
#ifndef SHELL_PASTE_MAX
#define SHELL_PASTE_MAX         1024U   /* 粘贴 / 块缓冲上限 */
#endif
#ifndef SHELL_BLOCK_DEPTH
#define SHELL_BLOCK_DEPTH       8U      /* 块嵌套 / 调用栈深度 */
#endif

/* ------------------------------ JOB ------------------------------ */
#ifndef SHELL_JOB_MAX
#define SHELL_JOB_MAX           4U      /* 并发 JOB 上限（槽位按需分配） */
#endif
#ifndef SHELL_JOB_PRIO
#define SHELL_JOB_PRIO          5U      /* 默认优先级（0 最高，idle=7） */
#endif
#ifndef SHELL_JOB_STACK
#define SHELL_JOB_STACK         2048U   /* 默认栈（字节） */
#endif
#ifndef SHELL_JOB_STACK_MIN
#define SHELL_JOB_STACK_MIN     512U
#endif
#ifndef SHELL_JOB_STACK_MAX
#define SHELL_JOB_STACK_MAX     8192U
#endif
#ifndef SHELL_SHELL_PRIO
#define SHELL_SHELL_PRIO        4U      /* Shell Task 优先级 */
#endif
#ifndef SHELL_SHELL_STACK
#define SHELL_SHELL_STACK       3072U
#endif

/* ------------------------------ 按需加载 ------------------------------ */
/* 监听任务栈（未激活 Shell 时的唯一常驻开销） */
#ifndef SHELL_WATCH_STACK
#define SHELL_WATCH_STACK       768U
#endif
/* 监听任务优先级（须小于 idle） */
#ifndef SHELL_WATCH_PRIO
#define SHELL_WATCH_PRIO        6U
#endif

/* ------------------------------ 等待 ------------------------------ */
#ifndef SHELL_WAIT_DEFAULT_MS
#define SHELL_WAIT_DEFAULT_MS   30000U  /* 默认等待超时 */
#endif
#ifndef SHELL_TID_TOMBSTONE_MS
#define SHELL_TID_TOMBSTONE_MS  5000U   /* TID 墓碑保留时间 */
#endif

/* ------------------------------ 变量 / 表达式 ------------------------------ */
#ifndef SHELL_VAR_MAX
#define SHELL_VAR_MAX           16U     /* 单张变量表容量 */
#endif
#ifndef SHELL_VAR_NAME_MAX
#define SHELL_VAR_NAME_MAX      17U     /* 变量名最大长度（不含 $） */
#endif
#ifndef SHELL_STR_MAX
#define SHELL_STR_MAX           48U     /* 字符串值最大长度 */
#endif
#ifndef SHELL_TOKEN_MAX
#define SHELL_TOKEN_MAX         48U     /* 单行最多 token 数 */
#endif
#ifndef SHELL_INSTR_MAX
#define SHELL_INSTR_MAX         64U     /* 单个脚本最多指令数 */
#endif

/* ------------------------------ 能力开关 ------------------------------ */
#ifndef SHELL_HAS_CPU_STATS
#define SHELL_HAS_CPU_STATS     1       /* ps 的 CPU% 列（来自 ros_task_cpu_permille） */
#endif
#ifndef SHELL_HAS_FS
#define SHELL_HAS_FS            0       /* 是否存在文件系统；0 时 run <file> 报 1002 */
#endif
#ifndef SHELL_LEGACY_ERRCODES
#define SHELL_LEGACY_ERRCODES   0       /* V1.0 错误码映射开关 */
#endif
#ifndef SHELL_SAFE_STOP_DEC_MS
#define SHELL_SAFE_STOP_DEC_MS  500U    /* kill / 异常时设备减速时间 */
#endif

#endif /* SHELL_CONFIG_H */
