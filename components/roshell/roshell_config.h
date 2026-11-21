#ifndef RS_CONFIG_H
#define RS_CONFIG_H

/*
 * roshell_config.h - roshell 编译期配置
 *
 * 集中存放资源上限、缓冲区尺寸与能力裁剪开关，可独立包含。
 * roshell.h 首部包含本文件；内部模块头经 roshell.h 间接获得这些宏。
 * 默认配置面向 20K RAM 设备（规格 §7）。
 */

/* ------------------------------ 能力开关 ------------------------------ */
/* 表达式/变量支持：0 时移除求值器与变量表，
 * if/while/for/赋值在扫描期报 unsupported，函数退化为无参形式。
 * 默认按平台选择：小 Flash 平台（stm32f1 系列）关闭，其余开放；
 * 仍可在构建时 -D RS_ENABLE_EXPR=0/1 覆盖 */
#ifndef RS_ENABLE_EXPR
#if defined(ROS_STM32_F1)
#define RS_ENABLE_EXPR   0
#else
#define RS_ENABLE_EXPR   1
#endif
#endif

/* 内置 math 函数库：依赖表达式求值器，默认跟随 RS_ENABLE_EXPR；
 * EXPR=0 时强制关闭（低端平台零成本），仍可显式 -D RS_ENABLE_MATH=0 裁剪 */
#ifndef RS_ENABLE_MATH
#define RS_ENABLE_MATH   RS_ENABLE_EXPR
#endif
#if !RS_ENABLE_EXPR
#undef RS_ENABLE_MATH
#define RS_ENABLE_MATH   0
#endif

/* ------------------------------ 缓冲区 ------------------------------ */
#ifndef RS_LINE_MAX
#define RS_LINE_MAX      128U    /* 单行最大字节数（含结尾 0） */
#endif
#ifndef RS_TOKEN_MAX
#define RS_TOKEN_MAX     48U     /* 单行最多 token 数（含 EOF） */
#endif
#ifndef RS_SCRIPT_MAX
#define RS_SCRIPT_MAX    4096U   /* 脚本源文本最大字节数 */
#endif

/* ------------------------------ 结构上限 ------------------------------ */
#ifndef RS_BLOCK_DEPTH
#define RS_BLOCK_DEPTH   8U      /* 块嵌套深度（块帧栈层数） */
#endif
#ifndef RS_FUNC_MAX
#define RS_FUNC_MAX      32U     /* 单脚本函数数 */
#endif
#ifndef RS_FUNC_ARGS_MAX
#define RS_FUNC_ARGS_MAX 8U      /* 单函数参数数 */
#endif
#ifndef RS_CALL_DEPTH
#define RS_CALL_DEPTH    4U      /* 调用嵌套深度 */
#endif
#ifndef RS_FOR_ITEMS_MAX
#define RS_FOR_ITEMS_MAX 16U     /* for 列表项数 */
#endif

/* ------------------------------ 变量 / 字符串 ------------------------------ */
#ifndef RS_VAR_MAX
#define RS_VAR_MAX       16U     /* 每执行单元变量数 */
#endif
#ifndef RS_VAR_NAME_MAX
#define RS_VAR_NAME_MAX  17U     /* 变量名最大长度（不含 $，含 0） */
#endif
#ifndef RS_STR_MAX
#define RS_STR_MAX       48U     /* 字符串值最大长度（含 0） */
#endif
#ifndef RS_NAME_MAX
#define RS_NAME_MAX      17U     /* 函数名最大长度（含 0） */
#endif

/* ------------------------------ run 任务 ------------------------------ */
#ifndef RS_RUN_MAX
#if defined(ROS_STM32_F1)
/* F103C8 仅 8KB TLSF 堆：槽位结构较大（约 630B/槽），需收紧 */
#define RS_RUN_MAX       2U
#else
#define RS_RUN_MAX       16U     /* 并发 run 任务槽数 */
#endif
#endif
#ifndef RS_RUN_PRIO
#define RS_RUN_PRIO      5U      /* run 任务默认优先级（0 最高） */
#endif
#ifndef RS_RUN_STACK
#if defined(ROS_STM32_F1)
#define RS_RUN_STACK     1024U   /* F1 堆受限，run 任务栈相应减小 */
#else
#define RS_RUN_STACK     4096U   /* run 任务默认栈字节数 */
#endif
#endif

/* ------------------------------ 扩展注册 ------------------------------ */
/* 全局扩展函数 / 常量注册表容量（EXPR；EXPR=0 时表被裁剪） */
#ifndef RS_EXT_FUNC_MAX
#define RS_EXT_FUNC_MAX  8U      /* rs_func_register 注册函数数 */
#endif
#ifndef RS_EXT_CONST_MAX
#define RS_EXT_CONST_MAX 8U      /* rs_const_register 注册常量数 */
#endif

/* ------------------------------ 会话 ------------------------------ */
/* waitjob 默认超时（会话配置，内建命令使用） */
#ifndef RS_SHELL_WAITJOB_MS
#define RS_SHELL_WAITJOB_MS    30000U
#endif

#endif /* RS_CONFIG_H */
