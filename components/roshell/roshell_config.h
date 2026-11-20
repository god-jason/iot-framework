#ifndef ROSHELL_CONFIG_H
#define ROSHELL_CONFIG_H

/*
 * roshell_config.h - roshell 脚本语言编译期配置
 *
 * 所有资源上限与裁剪开关集中在此，资源受限工程可按需收紧。
 * 默认配置面向 20K RAM 设备（规格 §7）。
 */

/* ------------------------------ 能力开关 ------------------------------ */
/* 表达式/变量支持：0 时移除求值器与变量表，
 * if/while/for/赋值在扫描期报 unsupported，函数退化为无参形式。
 * 默认按平台选择：小 Flash 平台（stm32f1 系列）关闭，其余开放；
 * 仍可在构建时 -D ROSHELL_ENABLE_EXPR=0/1 覆盖 */
#ifndef ROSHELL_ENABLE_EXPR
#if defined(ROS_STM32_F1)
#define ROSHELL_ENABLE_EXPR   0
#else
#define ROSHELL_ENABLE_EXPR   1
#endif
#endif

/* 文件系统：0 时 load 语句报 unsupported */
#ifndef ROSHELL_HAS_FS
#define ROSHELL_HAS_FS        1
#endif

/* ------------------------------ 缓冲区 ------------------------------ */
#ifndef ROSHELL_LINE_MAX
#define ROSHELL_LINE_MAX      128U    /* 单行最大字节数（含结尾 0） */
#endif
#ifndef ROSHELL_TOKEN_MAX
#define ROSHELL_TOKEN_MAX     48U     /* 单行最多 token 数（含 EOF） */
#endif
#ifndef ROSHELL_SCRIPT_MAX
#define ROSHELL_SCRIPT_MAX    4096U   /* 脚本源文本最大字节数 */
#endif

/* ------------------------------ 结构上限 ------------------------------ */
#ifndef ROSHELL_BLOCK_DEPTH
#define ROSHELL_BLOCK_DEPTH   8U      /* 块嵌套深度（块帧栈层数） */
#endif
#ifndef ROSHELL_FUNC_MAX
#define ROSHELL_FUNC_MAX      32U     /* 单脚本函数数 */
#endif
#ifndef ROSHELL_FUNC_ARGS_MAX
#define ROSHELL_FUNC_ARGS_MAX 8U      /* 单函数参数数 */
#endif
#ifndef ROSHELL_CALL_DEPTH
#define ROSHELL_CALL_DEPTH    4U      /* 调用嵌套深度 */
#endif
#ifndef ROSHELL_FOR_ITEMS_MAX
#define ROSHELL_FOR_ITEMS_MAX 16U     /* for 列表项数 */
#endif

/* ------------------------------ 变量 / 字符串 ------------------------------ */
#ifndef ROSHELL_VAR_MAX
#define ROSHELL_VAR_MAX       16U     /* 每执行单元变量数 */
#endif
#ifndef ROSHELL_VAR_NAME_MAX
#define ROSHELL_VAR_NAME_MAX  17U     /* 变量名最大长度（不含 $，含 0） */
#endif
#ifndef ROSHELL_STR_MAX
#define ROSHELL_STR_MAX       48U     /* 字符串值最大长度（含 0） */
#endif
#ifndef ROSHELL_NAME_MAX
#define ROSHELL_NAME_MAX      17U     /* 函数名最大长度（含 0） */
#endif

/* ------------------------------ run 任务 ------------------------------ */
#ifndef ROSHELL_RUN_MAX
#define ROSHELL_RUN_MAX       16U     /* 并发 run 任务槽数 */
#endif
#ifndef ROSHELL_RUN_PRIO
#define ROSHELL_RUN_PRIO      5U      /* run 任务默认优先级（0 最高） */
#endif
#ifndef ROSHELL_RUN_STACK
#define ROSHELL_RUN_STACK     4096U   /* run 任务默认栈字节数 */
#endif

#endif /* ROSHELL_CONFIG_H */
