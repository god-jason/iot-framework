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
 *   roshell_t *rs = rs_create();
 *   rs_err_t err;
 *   rs_exec(rs, "if $x == 1\nend\n", &err);
 *   rs_destroy(rs);
 *
 * 变量作用域：每个执行单元（脚本 / 函数调用）私有；实例保留一个
 * 顶层执行单元，rs_exec / rs_exec_line 共用其变量表，
 * 便于交互式逐行执行与测试观察（rs_get_* 读取顶层变量）。
 *
 * 本文件只保留对外类型与接口；编译期配置见 rs_config.h，
 * 各实现模块之间的共享声明见 roshell_<模块>.h（内部使用，不保证稳定）。
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "vec.h"
#include "map.h"
#include "roshell_config.h"
#include "roshell_args.h"
#include "roshell_cmd.h"
#include "ringbuf.h"
#include "roshell_lexer.h"
#include "roshell_eval.h"

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
    RS_ERR_TIMEOUT       = -15   /* 等待超时 */
};

/* 错误详情：code 为上述错误码，line 为出错行号（1 起，0 表示未知） */
typedef struct {
    int      code;
    uint16_t line;
    char     msg[48];
} rs_err_t;

/*
 * 按行取源回调（流式执行用，见 roshell_fs.h）：
 *   把第 idx 行（0 基）拷进 buf 并补 0，返回行长（0 = 空行）；
 *   负值为错误：-1 读失败，-2 行超长。
 * 实现需支持随机访问（解释器遇到 for/while 会回跳取行）。
 */
typedef int32_t (*rs_line_src_t)(void *ctx, uint16_t idx, char *buf, uint32_t cap);

/* 无行号错误填充（code 为 RS_ERR_*，msg 拷贝进 e；e 可为 NULL） */
void rs_err0(rs_err_t *e, int code, const char *msg);

/* ============================== 实例 ============================== */

/* roshell_t 的 typedef 见 roshell_cmd.h（本文件先包含该头） */
struct ros_task;
struct rs_prog;

/*
 * 命令 hook（规格 §8 statement -> command / §9 内置命令）：
 * 引擎解释语句时，行首 token 不是关键字 / 赋值 / 表达式 / 函数调用时，
 * 把整行交给 hook 分派。
 *   返回 1：已处理（*ret 为命令返回值，0 表示成功，非 0 为错误码，
 *           引擎将中止脚本执行并把该码返回给调用方）；
 *   返回 0：不是命令（引擎按表达式语句继续处理）；
 *   返回负值：hook 自身错误，引擎中止脚本执行。
 */
typedef int (*rs_cmd_hook_t)(roshell_t *rs, const char *line, int *ret);

/*
 * 实例即唯一的上下文：顶层执行单元 + 运行状态 + 命令 hook + 交互会话状态。
 * rs_shell_start 新建一个实例就是一个独立 shell，会话数据不需要额外结构。
 */
struct roshell {
    rs_unit_t            top;             /* 顶层执行单元（变量表 / token 缓冲 / 块帧） */
    rs_cmd_hook_t        cmd_hook;        /* 块内命令路由（规格 §8 / §9） */
    vec_t                sleeps;          /* sleep 持续命令状态（单表项，惰性初始化） */

    /* -------------------- 运行期执行状态（单实例即单 context） -------------------- */
    struct rs_prog      *prog;            /* 当前程序对象（rs_exec 期间有效） */
    rs_unit_t           *unit;            /* 当前执行单元：&top 或函数调用子单元 */
    uint8_t              depth;           /* 函数调用深度 */
    rs_err_t            *err;             /* 当前错误输出，可为 NULL */
    /* 按行源（文件等）取行缓冲：按调用深度分开，函数嵌套时不互相覆盖 */
    char                 lbuf[RS_CALL_DEPTH + 1U][RS_LINE_MAX];

    /* -------------------- 交互会话（roshell_repl / roshell_io 初始化） -------------------- */
    struct ros_task     *task;            /* 会话输入任务 */
    const char          *version;         /* banner / ver 版本串 */
    struct ros_io       *in;              /* 流式输入（roshell_io 绑定；NULL 表示手工投喂） */
    struct ros_io       *out;             /* 流式输出（NULL 表示当前任务 stdio） */
    bool                 eof;             /* 输入流已结束（EOF / 读错误） */
    ringbuf_t            rx;              /* 接收环形缓冲（feed 模式） */
    uint8_t              rx_mem[RS_SHELL_RX_MAX];
    char                 line[RS_LINE_MAX];
    char                 cwd[RS_CWD_MAX]; /* 当前工作目录，默认 "/" */
    uint32_t             line_len;
    bool                 last_cr;
    bool                 last_esc;
    volatile bool        started;         /* 已作为交互会话启动 */
    volatile bool        finished;        /* 输入任务已退出（可回收） */
    volatile bool        exit_req;        /* exit 命令 / 平台 EOF 请求 */
};

/* 注册 / 清除命令 hook（hook 传 NULL 清除）；hook 直接收到实例 rs */
void rs_set_cmd_hook(roshell_t *rs, rs_cmd_hook_t hook);

/* 创建实例（堆分配词法缓冲 / 变量表）；失败返回 NULL */
roshell_t *rs_create(void);

/* 销毁实例并释放全部堆内存 */
void rs_destroy(roshell_t *rs);

/* ============================== 执行 ============================== */

/*
 * 扫描并同步执行整段脚本（多行，块语句完整）。
 * 返回 RS_OK 或错误码；err 可为 NULL。
 */
int rs_exec(roshell_t *rs, const char *script, rs_err_t *err);

/*
 * 交互式执行单行（不留存全文）。块关键字报
 * "block statement requires script"。返回同上。
 */
int rs_exec_line(roshell_t *rs, const char *line, rs_err_t *err);

/* ============================== 顶层变量读取 ============================== */
/* 供上位机 / 测试观察顶层执行单元变量；name 可带 $ 前缀。
 * 未定义返回 RS_ERR_UNDEF_VAR，类型不符返回 RS_ERR_TYPE。 */

int rs_get_int(roshell_t *rs, const char *name, int32_t *out);
int rs_get_float(roshell_t *rs, const char *name, float *out);
int rs_get_bool(roshell_t *rs, const char *name, bool *out);
/* 拷贝字符串值到 buf（含结尾 0，截断安全） */
int rs_get_str(roshell_t *rs, const char *name, char *buf, uint32_t cap);

/* ============================== 扩展注册（函数 / 常量） ============================== */

/* 扩展函数实参（与引擎内部值类型对齐） */
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
 * 扩展函数签名：args 为引擎求值后的实参（INT / FLOAT 原样传入，
 * BOOL 以 0/1 整数传入；字符串实参在调用前即报 type mismatch），
 * nargs 与注册时的 argc 一致。返回值仅接受 RS_ARG_INT /
 * RS_ARG_FLOAT / RS_ARG_BOOL（其余按 type mismatch 处理）。
 */
typedef rs_arg_t (*rs_func_fn_t)(roshell_t *rs, const rs_arg_t *args,
                                 uint32_t nargs);

/*
 * 注册全局扩展函数：脚本中以 name(实参...) 调用，不绑定实例。
 * 查找优先级：内置函数 -> 扩展函数 -> 脚本 function。
 * name 需为标识符（字母 / 下划线开头，长度 < RS_NAME_MAX）。
 * 返回 RS_OK；name / argc 非法或与内置函数、内置常量、已注册扩展名
 * 重名返回 RS_ERR_PARAM；表满（RS_EXT_FUNC_MAX）返回 RS_ERR_LIMIT。
 * 不支持反注册。
 */
int rs_func_register(const char *name, uint8_t argc, rs_func_fn_t fn);

/*
 * 注册全局扩展常量：脚本中以裸标识符引用（如 $x = PI * 2），值为浮点。
 * 查找优先级：内置常量 -> 扩展常量（$x 变量为另一命名空间，互不影响）。
 * 返回值同 rs_func_register（容量为 RS_EXT_CONST_MAX）。
 */
int rs_const_register(const char *name, float value);

/* ============================== 命令分派 ============================== */

/*
 * 形态：在 roshell 脚本引擎之上提供一组小写、大小写敏感的内建命令
 * （help/ver/echo/ps/kill/pause/resume/mem/state/sleep/wait/exit），
 * 并把串口（STM32）或标准输入输出（PC）的字节流组装成行驱动会话。
 *
 * 分派规则（rs_cmd_dispatch）：
 *   1. 行首 token 命中内建命令表 -> 执行命令；
 *   2. 否则查 rs_cmd 全局命令表（见本文件命令表一节，设备/组件命令走这里）；
 *   3. 仍未命中 -> 整行交给 roshell 脚本引擎（rs_exec_line）。
 *
 * 会话生命周期（每个会话一个句柄 roshell_t*，平台层只依赖本头文件）：
 *   rs_shell_start()   新建独立会话与输入任务，返回句柄，打印 banner 与提示符；
 *   rs_shell_feed()    串口 ISR / 轮询投喂字节（进该会话的接收环形缓冲）；
 *   rs_shell_process() 消费缓冲：组行 + 分派（输入任务内部调用，
 *                           无输入任务的平台 / 测试也可直接驱动）；
 *   rs_shell_started() / rs_shell_task_finished() 状态查询；
 *   rs_shell_shutdown() 输入任务退出后回收任务与会话内存。
 *
 * 行编辑大幅简化：回显、退格、CRLF 归一、Ctrl-C 清行、ESC 序列吞掉；
 * 无历史与 Tab 补全。
 */

/*
 * 执行一行输入：内建命令 -> rs_cmd 全局命令表 -> roshell 脚本引擎。
 * 返回 0 成功；负值为 RS_ERR_* 或命令错误码。rs 可为 NULL
 * （仅脚本路径与需要实例的命令返回 -RS_ERR_PARAM）。
 */
int rs_cmd_dispatch(roshell_t *rs, const char *line);

/*
 * 给实例安装命令 hook（rs_set_cmd_hook）：脚本块内的命令行
 * （sleep / wait / echo 等，规格 §8 statement -> command）经
 * 内建命令表 -> rs_cmd 全局命令表分派，不进入脚本引擎语句解释。
 */
void rs_cmd_install(roshell_t *rs);

/* ============================== 模块划分 ============================== */
/*
 * 本文件只声明“引擎本体”：实例生命周期、执行、顶层变量读取、执行上下文。
 * 其余能力按用途拆到独立模块，按需包含：
 *   roshell_repl.h  REPL 会话外壳 rs_shell_*（环形缓冲 + 输入任务 + cwd/exit）
 *   roshell_io.h    基于 ros_io_t 的流式会话（rs_repl_open/poll/run）
 *   roshell_fs.h    直接执行文件 / 按行源（流式，不整读）
 *   roshell_cmd.h   命令表与分派（rs_cmd_register / rs_cmd_dispatch）
 *   roshell_args.h  命令参数解析
 */

/* 参数解析见 roshell_args.h；命令定义与全局命令表见 roshell_cmd.h。 */

#ifdef __cplusplus
}
#endif

#endif /* RS_H */
