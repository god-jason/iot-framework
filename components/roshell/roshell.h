#ifndef RS_H
#define RS_H

/*
 * roshell.h - roshell 对外类型与接口
 *
 * 形态：运行在 RTOS 之上的精简脚本解释器（语法见 roshell.md）。
 * 执行模型：直接解释源文本，不生成字节码；加载时仅做一次轻量扫描
 * （校验块配对、登记函数名与起始行），运行期按行号就地词法分析。
 *
 * 典型用法：
 *   rs_t *rs = rs_create();
 *   rs_err_t err;
 *   rs_exec(rs, "if $x == 1\nend\n", &err);
 *   rs_destroy(rs);
 *
 * 变量作用域：每个执行单元（脚本 / 函数调用）私有；实例保留一个
 * 顶层执行单元，rs_exec / rs_exec_line / load 共用其变量表，
 * 便于交互式逐行执行与测试观察（rs_get_* 读取顶层变量）。
 * run 派生任务拥有独立执行单元。
 *
 * 本文件只保留对外类型与接口；编译期配置见 rs_config.h，
 * 各实现模块之间的共享声明见 roshell_<模块>.h（内部使用，不保证稳定）。
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "vec.h"
#include "roshell_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================== 错误码 ============================== */

enum {
    RS_OK = 0,
    RS_ERR_PARAM         = -1,   /* 参数非法 */
    RS_ERR_PARSE         = -2,   /* 词法 / 语法错误（unknown statement 等） */
    RS_ERR_TYPE          = -3,   /* type mismatch */
    RS_ERR_UNDEF_VAR     = -4,   /* 变量未定义 */
    RS_ERR_UNDEF_FUNC    = -5,   /* undefined function */
    RS_ERR_DIV_ZERO      = -6,   /* 取余除零 */
    RS_ERR_RESOURCE      = -7,   /* 内存不足 / 任务创建失败 */
    RS_ERR_UNSUPPORTED   = -8,   /* 编译期裁剪掉的能力 */
    RS_ERR_NOT_FOUND     = -9,   /* 文件不可读 / 槽位无效 */
    RS_ERR_LIMIT         = -10,  /* 超出资源上限（行长 / 脚本长 / 列表项等） */
    RS_ERR_STACK_OVERFLOW = -11, /* call stack overflow */
    RS_ERR_ARG_COUNT     = -12,  /* arg count mismatch */
    RS_ERR_DUPLICATE     = -13,  /* duplicate function */
    RS_ERR_BLOCK         = -14,  /* unclosed block / unmatched end */
    RS_ERR_TIMEOUT       = -15   /* run 等待超时 */
};

/* run 槽位状态 */
enum {
    RS_RUN_FREE = 0,
    RS_RUN_RUNNING,
    RS_RUN_DONE,
    RS_RUN_ERROR
};

/* 错误详情：code 为上述错误码，line 为出错行号（1 起，0 表示未知） */
typedef struct {
    int      code;
    uint16_t line;
    char     msg[48];
} rs_err_t;

/* ============================== 实例 ============================== */

typedef struct roshell rs_t;

/*
 * 命令 hook（规格 §8 statement -> command / §9 内置命令）：
 * 引擎解释语句时，行首 token 不是关键字 / 赋值 / 表达式 / 函数调用时，
 * 把整行交给 hook 分派。
 *   返回 1：已处理（*ret 为命令返回值，0 表示成功，非 0 为错误码，
 *           引擎将中止脚本执行并把该码返回给调用方）；
 *   返回 0：不是命令（引擎按表达式语句继续处理）；
 *   返回负值：hook 自身错误，引擎中止脚本执行。
 */
typedef int (*rs_cmd_hook_t)(void *ctx, const char *line, int *ret);

/* 注册 / 清除命令 hook（hook 传 NULL 清除）；ctx 原样回传给 hook */
void rs_set_cmd_hook(rs_t *rs, rs_cmd_hook_t hook, void *ctx);

/* 创建实例（堆分配词法缓冲 / 变量表 / run 槽表）；失败返回 NULL */
rs_t *rs_create(void);

/* 销毁实例并释放全部堆内存；已结束的 run 任务 TCB 一并回收 */
void rs_destroy(rs_t *rs);

/* ============================== 执行 ============================== */

/*
 * 扫描并同步执行整段脚本（多行，块语句完整）。
 * 返回 RS_OK 或错误码；err 可为 NULL。
 */
int rs_exec(rs_t *rs, const char *script, rs_err_t *err);

/*
 * 交互式执行单行（不留存全文）。块关键字报
 * "block statement requires script"。返回同上。
 */
int rs_exec_line(rs_t *rs, const char *line, rs_err_t *err);

/* ============================== run 槽位（内置命令 run 的底层） ============================== */

/* run 实参类型（与引擎内部值类型对齐；规格 §9：字面量或 $var，不做表达式求值） */
enum {
    RS_ARG_INT = 1,
    RS_ARG_FLOAT,
    RS_ARG_BOOL,
    RS_ARG_STR
};

typedef struct {
    uint8_t     type;   /* RS_ARG_* */
    int32_t     i;
    float       f;
    bool        b;
    const char *s;      /* STR：内容指针 + 长度（不含结尾 0） */
    uint16_t    slen;
} rs_arg_t;

/*
 * 异步执行已登记函数（内置命令 run 的实现）：新建任务立即返回槽位号。
 * 函数在会话最近一次 rs_exec / rs_load 登记的脚本中查找；
 * 实参个数须与形参一致（RS_ENABLE_EXPR=0 时仅支持 0 个实参）。
 * 返回槽位号（>=0）；负值为错误码（RS_ERR_UNDEF_FUNC / RS_ERR_ARG_COUNT /
 * RS_ERR_LIMIT / RS_ERR_RESOURCE 等）。
 */
int rs_run_async(rs_t *rs, const char *fn,
                      const rs_arg_t *args, int nargs);

/* 等待槽位任务结束；返回 RS_OK / RS_ERR_TIMEOUT / 任务错误码 / RS_ERR_PARAM */
int rs_run_wait(rs_t *rs, int slot, uint32_t timeout_ms);

/* 查询槽位状态（RS_RUN_*）；槽位号非法返回 RS_ERR_PARAM */
int rs_run_state(const rs_t *rs, int slot);

/* 占用中（非 FREE）的槽位数 */
int rs_run_count(const rs_t *rs);

/* 槽位正在运行的函数名；空闲 / 整脚本槽 / 槽位号非法返回 "" */
const char *rs_run_fname(const rs_t *rs, int slot);

#if RS_HAS_FS
/* 读取文件并同步执行（解析失败报告文件内行号）。返回同 rs_exec */
int rs_load(rs_t *rs, const char *path, rs_err_t *err);
#endif

/* ============================== 顶层变量读取 ============================== */
/* 供上位机 / 测试观察顶层执行单元变量；name 可带 $ 前缀。
 * 未定义返回 RS_ERR_UNDEF_VAR，类型不符返回 RS_ERR_TYPE。 */

int rs_get_int(rs_t *rs, const char *name, int32_t *out);
int rs_get_float(rs_t *rs, const char *name, float *out);
int rs_get_bool(rs_t *rs, const char *name, bool *out);
/* 拷贝字符串值到 buf（含结尾 0，截断安全） */
int rs_get_str(rs_t *rs, const char *name, char *buf, uint32_t cap);

/* ============================== 扩展注册（函数 / 常量） ============================== */

/*
 * 扩展函数签名：args 为引擎求值后的实参（INT / FLOAT 原样传入，
 * BOOL 以 0/1 整数传入；字符串实参在调用前即报 type mismatch），
 * nargs 与注册时的 argc 一致。返回值仅接受 RS_ARG_INT /
 * RS_ARG_FLOAT / RS_ARG_BOOL（其余按 type mismatch 处理）。
 */
typedef rs_arg_t (*rs_func_fn_t)(const rs_arg_t *args,
                                           uint32_t nargs);

/*
 * 注册全局扩展函数：脚本中以 name(实参...) 调用，不绑定实例。
 * 查找优先级：内置函数 -> 扩展函数 -> 脚本 function。
 * name 需为标识符（字母 / 下划线开头，长度 < RS_NAME_MAX）。
 * 返回 RS_OK；name / argc 非法或与内置函数、内置常量、已注册扩展名
 * 重名返回 RS_ERR_PARAM；表满（RS_EXT_FUNC_MAX）返回 RS_ERR_LIMIT；
 * RS_ENABLE_EXPR=0 时返回 RS_ERR_UNSUPPORTED。不支持反注册。
 */
int rs_func_register(const char *name, uint8_t argc, rs_func_fn_t fn);

/*
 * 注册全局扩展常量：脚本中以裸标识符引用（如 $x = PI * 2），值为浮点。
 * 查找优先级：内置常量 -> 扩展常量（$x 变量为另一命名空间，互不影响）。
 * 返回值同 rs_func_register（容量为 RS_EXT_CONST_MAX）。
 */
int rs_const_register(const char *name, double value);

/* ============================== 命令分派 ============================== */

/*
 * 形态：在 roshell 脚本引擎之上提供一组小写、大小写敏感的内建命令
 * （help/ver/echo/ps/kill/pause/resume/mem/state/jobs/waitjob/run/wait/load/exit），
 * 并把串口（STM32）或标准输入输出（PC）的字节流组装成行驱动会话。
 *
 * 分派规则（rs_cmd_dispatch）：
 *   1. 行首 token 命中内建命令表 -> 执行命令；
 *   2. 否则查 rs_cmd 全局命令表（见本文件命令表一节，设备/组件命令走这里）；
 *   3. 仍未命中 -> 整行交给 roshell 脚本引擎（rs_exec_line）。
 *
 * 会话生命周期（对应旧 shell 的用法，平台层只依赖本头文件）：
 *   rs_shell_start()   创建实例与输入任务，打印 banner 与提示符；
 *   rs_shell_feed()    串口 ISR / 轮询投喂字节（进接收环形缓冲）；
 *   rs_shell_process() 消费缓冲：组行 + 分派（输入任务内部调用，
 *                           无输入任务的平台 / 测试也可直接驱动）；
 *   rs_shell_started() / rs_shell_task_finished() 状态查询；
 *   rs_shell_shutdown() 输入任务退出后回收任务与实例内存。
 *
 * 行编辑大幅简化：回显、退格、CRLF 归一、Ctrl-C 清行、ESC 序列吞掉；
 * 无历史与 Tab 补全。
 */

/*
 * 执行一行输入：内建命令 -> rs_cmd 全局命令表 -> roshell 脚本引擎。
 * 返回 0 成功；负值为 RS_ERR_* 或命令错误码。rs 可为 NULL
 * （仅脚本路径与需要实例的命令返回 -RS_ERR_PARAM）。
 */
int rs_cmd_dispatch(rs_t *rs, const char *line);

/*
 * 给实例安装命令 hook（rs_set_cmd_hook）：脚本块内的命令行
 * （wait / load / run / echo 等，规格 §8 statement -> command）经
 * 内建命令表 -> rs_cmd 全局命令表分派，不进入脚本引擎语句解释。
 */
void rs_cmd_install(rs_t *rs);

/* ============================== 交互会话 ============================== */

/*
 * 创建 roshell 实例与输入任务（任务名 "roshell_io"），打印 banner 与
 * 提示符。version 仅用于 ver 命令显示，可为 NULL。
 * 幂等：已启动直接返回 0；内存不足返回 -1。
 */
int rs_shell_start(const char *version);

/* 投喂输入字节（串口 ISR / 轮询可调用）：追加到接收环形缓冲；
 * 会话未启动或缓冲满时丢弃。 */
void rs_shell_feed(const char *data, uint32_t len);

/* 消费接收缓冲中的全部字节：组行并分派，返回提交的行数（回车计数）。
 * 输入任务循环调用；测试或无任务平台可手动驱动。 */
uint32_t rs_shell_process(void);

bool rs_shell_started(void);
bool rs_shell_task_finished(void);

/* 请求关闭会话（exit 命令 / 平台输入 EOF）：输入任务在本轮后退出 */
void rs_shell_request_exit(void);

/*
 * 回收会话：要求 rs_shell_task_finished() 为真。
 * 销毁输入任务与 roshell 实例，回到零占用状态。返回 0 / -1。
 */
int rs_shell_shutdown(void);

/* 会话 roshell 实例（未启动返回 NULL），供上位机 / 测试观察 */
rs_t *rs_shell_instance(void);

/* 当前编辑行已积累的字节数（未提交部分），供测试观察截断行为 */
uint32_t rs_shell_line_pending(void);

/* ============================== 平台默认绑定 ============================== */

/*
 * 平台层实现（platforms/pc/pc_shell.c、platforms/stm32/stm32_shell.c）：
 *   PC    : stdin 逐字节 feed 到会话；上电只跑监听任务，
 *           输入口令 "hello" 后才创建会话；exit 回收后回到监听；
 *           监听态 quit / 管道 EOF 停止内核。
 *   STM32 : UART1 轮询逐字节 feed；同样口令 "hello" 开启会话。
 * 返回 0 成功；平台不支持或资源不足返回 -1。
 */
int rs_platform_start(const char *version);

/* ============ 参数解析（原 args 模块） ============ */

/*
 * 命令参数定义与解析（与命令表解耦）
 *
 * 用 rs_args_t 描述参数，用 rs_args_parse() 解析
 * "--name value"、"--name=value"、"-s value"、布尔开关及位置参数，
 * 再通过 rs_args_get_*() 按定义下标取值。
 *
 * 参数模型：
 *   - positional=true 为位置参数（按声明顺序绑定，name 仅作显示标签）；
 *   - positional=false 为选项：--name / -x（short_name 非 0 时）；
 *   - RS_ARGS_TYPE_BOOL 为开关，不带值（也接受 --flag=0/1/on/off）；
 *   - required=true 的参数必须出现（默认值不会满足 required）。
 *
 * 所有文本均为只读引用，不分配内存。
 */

#ifndef RS_ARGS_MAX
#define RS_ARGS_MAX            12U     /* 单条命令最多参数个数 */
#endif
#ifndef RS_ARGS_NAME_MAX
#define RS_ARGS_NAME_MAX       20U     /* 参数名最大长度（含结尾 0） */
#endif

/* 错误码 */
#define RS_ARGS_OK                 0
#define RS_ARGS_ERR_PARAM          (-1)    /* 参数非法（NULL / 下标越界） */
#define RS_ARGS_ERR_UNKNOWN        (-2)    /* 未知选项 */
#define RS_ARGS_ERR_MISSING_VALUE  (-3)    /* 选项缺少取值 */
#define RS_ARGS_ERR_MISSING_ARG    (-4)    /* 缺少必填参数 */
#define RS_ARGS_ERR_TYPE           (-5)    /* 取值类型错误 */
#define RS_ARGS_ERR_EXTRA          (-6)    /* 多余的位置参数 */

/* 参数类型 */
typedef enum {
    RS_ARGS_TYPE_STR = 0,      /* 字符串 */
    RS_ARGS_TYPE_INT,          /* 有符号整数 */
    RS_ARGS_TYPE_UINT,         /* 无符号整数 */
    RS_ARGS_TYPE_BOOL          /* 布尔开关 */
} rs_args_type_t;

/* 单个参数定义 */
typedef struct {
    const char *name;       /* 显示名（位置参数即标签；选项为长名，不含 --） */
    char        short_name; /* 短选项名（不含 -）；0 表示无 */
    uint8_t     type;       /* rs_args_type_t */
    bool        positional; /* true 表示位置参数 */
    bool        required;   /* 是否必须出现 */
    const char *def;        /* 默认值（文本），可为 NULL */
    const char *desc;       /* 参数说明，可为 NULL */
} rs_args_t;

/* 解析结果（由调用者分配，rs_args_parse 填充） */
typedef struct {
    const rs_args_t *args;             /* 参数定义表（由解析时绑定） */
    uint8_t     arg_count;              /* 参数个数 */
    const char *vals[RS_ARGS_MAX];         /* 命中的原始值（BOOL 可为 NULL） */
    bool        present[RS_ARGS_MAX];      /* 是否出现 */
    uint8_t     positionals;            /* 已绑定位置参数个数 */
    int         error;                  /* RS_ARGS_OK 或 RS_ARGS_ERR_* */
    char        err_arg[RS_ARGS_NAME_MAX]; /* 出错参数名 */
} rs_args_parsed_t;

/*
 * 解析参数。
 * args      : 参数定义表；
 * arg_count : 参数个数；
 * argv      : 参数数组，不含命令名本身；
 * argc      : 参数个数；
 * parsed    : 输出解析结果。
 * 返回      : RS_ARGS_OK 或负错误码（同时写入 parsed->error）。
 */
int rs_args_parse(const rs_args_t *args, uint8_t arg_count,
               char *const *argv, int argc, rs_args_parsed_t *parsed);

/* 按长名 / 短名查找参数下标，未找到返回 -1 */
int rs_args_find(const rs_args_t *args, uint8_t arg_count, const char *name);
int rs_args_find_short(const rs_args_t *args, uint8_t arg_count,
                    char short_name);

/*
 * 取值：未出现时回退到定义中的默认值。
 * index 为参数在定义表中的下标。
 * 返回 false 表示下标非法、无默认值或类型转换失败。
 */
bool rs_args_get_str(const rs_args_parsed_t *parsed, uint8_t index, const char **out);
bool rs_args_get_int(const rs_args_parsed_t *parsed, uint8_t index, int *out);
bool rs_args_get_uint(const rs_args_parsed_t *parsed, uint8_t index, uint32_t *out);
bool rs_args_get_bool(const rs_args_parsed_t *parsed, uint8_t index, bool *out);

/* 错误码转文本 */
const char *rs_args_error_str(int err);

/* ============ 命令表（原 cmd 组件） ============ */

#ifndef RS_CMD_NAME_MAX
#define RS_CMD_NAME_MAX        24U     /* 命令名最大长度（含结尾 0） */
#endif

struct ros_task;

/* 命令执行上下文（内建与扩展命令统一入口参数） */
typedef struct {
    rs_t *rs;                 /* 会话实例；非会话来源为 NULL */
    int argc;
    char **argv;
    const rs_args_parsed_t *parsed;/* 参数表解析结果；无参数表为 NULL */
    void *user;                    /* def->user */
} rs_cmd_ctx_t;

/* 命令处理函数（内建与扩展命令统一签名） */
typedef int (*rs_cmd_fn_t)(const rs_cmd_ctx_t *ctx);

/* 命令生命周期回调（可选）：user 为命令定义中的处理函数上下文 def->user，
 * task 为命令正在执行的任务（由 rs_cmd_notify_* 传入） */
typedef int (*rs_cmd_hook_fn_t)(void *user, struct ros_task *task);

/* 命令定义（内建与扩展命令共用；usage 为空时帮助文本由参数表动态生成） */
typedef struct rs_cmd {
    const char *name;       /* 命令名 */
    const char *desc;       /* 一句话说明 */
    const char *usage;      /* 用法行；NULL 时由参数表动态生成（rs_cmd_usage） */
    const char *group;      /* help 分组（内建命令用）；扩展命令可为 NULL */
    const rs_args_t *args;
    uint8_t     arg_count;
    rs_cmd_fn_t fn;            /* 处理函数 */
    void       *user;       /* 处理函数上下文（可为 NULL） */

    /* 生命周期钩子，均为可选（NULL 表示不处理）：
     * on_pause  在命令所在任务被挂起前调用，返回 <0 可拒绝暂停
     *           （wait 命令约定：0=钩子已接管暂停，-1=未登记、调用方回退 OS 挂起）；
     * on_resume 在任务恢复后调用；
     * on_kill   在命令/任务被停止或终止前调用。 */
    rs_cmd_hook_fn_t on_pause;
    rs_cmd_hook_fn_t on_resume;
    rs_cmd_hook_fn_t on_kill;
} rs_cmd_t;

/*
 * 生成 usage 单行文本，例如：
 *   ping <host> [--count|-c <n>] [--verbose]
 * def->usage 非 NULL 时直接输出该串，否则由参数表生成。
 * 返回写入长度；缓冲区不足返回 -1。
 */
int rs_cmd_usage(const rs_cmd_t *def, char *buf, uint32_t size);

/*
 * 生成多行帮助：说明 + usage + 参数列表。
 * 返回写入长度；缓冲区不足返回 -1。
 */
int rs_cmd_help(const rs_cmd_t *def, char *buf, uint32_t size);

/*
 * 在命令表中查找 argv[0] 并解析其余参数，解析成功后调用处理函数。
 * user 为默认上下文：命令 def->user 为 NULL 时使用它。
 * 返回处理函数返回值；未找到命令返回 RS_ARGS_ERR_UNKNOWN；
 * 解析失败返回对应 RS_ARGS_ERR_*。
 */
int rs_cmd_run(const rs_cmd_t *table, uint32_t count,
            int argc, char **argv, void *user);

/* ============================== 全局命令表 ============================== */

/* 全局命令表（定义在 rs_cmd.c；按注册顺序追加，存 rs_cmd_t 值拷贝） */
extern vec_t rs_cmd_table;

/*
 * 注册命令到全局命令表（入表为 def 的值拷贝）。
 * def->name / def->fn 为空、名称重复（不区分大小写）时返回 -1；成功返回 0。
 */
int rs_cmd_register(const rs_cmd_t *def);

/* 清空并释放全局命令表（测试 / 反初始化用） */
void rs_cmd_unregister_all(void);

/* 在全局命令表中查找（名称不区分大小写），未找到返回 NULL */
const rs_cmd_t *rs_cmd_find_command(const char *name);

/* 遍历全局命令表：首个命令 / 后继命令（到尾部返回 NULL） */
const rs_cmd_t *rs_cmd_first(void);
const rs_cmd_t *rs_cmd_next(const rs_cmd_t *def);

/* 解析参数并执行一条已找到的命令 */
int rs_cmd_run_node(const rs_cmd_t *def, int argc, char **argv);

/*
 * 按 argv[0] 在全局命令表中查找并执行。
 * 未找到返回 RS_ARGS_ERR_UNKNOWN；解析失败返回 RS_ARGS_ERR_*；
 * 否则返回处理函数的返回值。
 */
int rs_builtin_dispatch(int argc, char **argv);

/* ============================== 生命周期通知 ============================== */
/*
 * 由 shell 的 pause/resume/kill 等入口调用：查找指定任务当前正在执行的
 * 命令，并调用其 on_pause / on_resume / on_kill 钩子（未实现则忽略）。
 * 钩子在调用者任务上下文中同步执行，不应长时间阻塞。
 * rs_cmd_notify_kill 会在回调后清除该任务的命令登记。
 */
int rs_cmd_notify_pause(const struct ros_task *task);
int rs_cmd_notify_resume(const struct ros_task *task);
int rs_cmd_notify_kill(const struct ros_task *task);

#ifdef __cplusplus
}
#endif

#endif /* RS_H */
