#ifndef SHELL_H
#define SHELL_H

/*
 * shell.h - IoT Shell V1.1 公共接口（多实例）
 *
 * 形态：运行在 RTOS 之上的交互式命令行。每个 shell_t 是独立实例，
 * 拥有自己的变量表、JOB 表、行编辑 / 历史 / 粘贴缓冲和输出后端：
 *   - 默认实例：由 shell_setup() 创建，平台层绑定 UART1 或 stdin/stdout；
 *   - 虚拟实例：shell_instance_create() 创建，用于机器人任务、文件/网络
 *     会话或嵌套执行，输出可接到任意 write 回调。
 *
 * 能力：
 *   - SHELL_CTX：内建命令（help/ver/echo/print/set/unset/wait/ps/kill/
 *     pause/resume/load/loads/unload/run/start/stop/fg/jobs/job/waitjob/state）
 *   - VM_CTX：脚本语句（SET/PRINT/IF/FOR/WHILE/LOOP/BREAK/CONTINUE/
 *     WAIT TIME/START JOB/STOP JOB/ON ERROR/ON STOP/ENDJOB/LABEL/GOTO/CALL/RETURN/
 *     SHELL PRINT|LOG|STATE|PS）
 *   - 每个 JOB 由独立 RTOS 任务承载；shell_exec_script() 提供不建任务的同步执行。
 *   - `run` 命令与 shell_exec_isolated() 会在新实例中解释执行脚本。
 *
 * 未实现（按 V1.1 §8 范围边界）：设备层（STEP/PWM/GPIO…）、文件系统、
 * 扩展命令段自动注册（用 shell_register_cmd() 显式注册）、调试模式。
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "shell_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================== 错误码 ============================== */

enum {
    SHELL_OK = 0,
    SHELL_ERR_PARAM      = 1001,   /* 参数 / 取值错误 */
    SHELL_ERR_NOT_FOUND  = 1002,   /* 任务 / 文件不存在 */
    SHELL_ERR_DEVICE     = 1003,   /* 设备执行失败 */
    SHELL_ERR_TIMEOUT    = 1004,   /* 等待超时 */
    SHELL_ERR_SENSOR     = 1005,
    SHELL_ERR_MOTOR      = 1006,
    SHELL_ERR_LIMIT      = 1007,
    SHELL_ERR_PROGRAM    = 1008,
    SHELL_ERR_UNDEF_VAR  = 1009,   /* 变量未定义 */
    SHELL_ERR_TYPE       = 1010,   /* 类型不匹配 */
    SHELL_ERR_DIV_ZERO   = 1011,   /* 除零 / 取余零 */
    SHELL_ERR_PARSE      = 1012,   /* 词法 / 语法错误 */
    SHELL_ERR_READONLY   = 1013,   /* 变量只读 */
    SHELL_ERR_BUSY       = 1014,
    SHELL_ERR_NOT_CLAIM  = 1015,
    SHELL_ERR_DANGER     = 1016,   /* 危险操作被拒绝（如删 shell/idle） */
    SHELL_ERR_RESOURCE   = 1017,   /* 资源耗尽 */
    SHELL_ERR_NAME       = 1018,   /* 名称冲突 / 保留名 */
    SHELL_ERR_STATE      = 1019,   /* 当前状态不允许 */
    SHELL_ERR_JUMP       = 1020    /* 非法跳转 / 环形等待 */
};

/* 扩展命令返回值：0 成功，-<code> 失败 */
#define SHELL_CMD_OK        0
#define SHELL_CMD_ERR(code) (-(code))

/* ============================== 实例 ============================== */

/* 不透明实例类型（定义在 shell_internal.h） */
typedef struct shell shell_t;

/* ============================== 扩展命令 ============================== */

typedef enum {
    CMD_NONE      = 0,
    CMD_HIDDEN    = (1U << 1),   /* help 不列出 */
    CMD_REENTRANT = (1U << 2)    /* 允许被 SHELL <cmd> 透传 */
} shell_cmd_flags_t;

/* 命令处理函数：sh 为调用该命令的实例（输出 / 变量都绑定到它） */
typedef int (*shell_cmd_fn)(shell_t *sh, int argc, char **argv);

typedef struct shell_cmd {
    const char *name;
    shell_cmd_fn handler;
    const char *usage;
    const char *help;
    const char *group;
    uint32_t    flags;
} shell_cmd_t;

/* 注册扩展命令（全局命令表，所有实例共享；重复名/与基础命令同名会被拒绝） */
int shell_register_cmd(const shell_cmd_t *cmd);

/* ============================== 配置 ============================== */

typedef struct {
    const char *version;                              /* 版本字符串 */
    int (*write)(const char *data, uint32_t len, void *user);  /* 可空：丢弃输出 */
    void *user;
    void (*reset)(void);                              /* reboot 钩子，可空 */
} shell_config_t;

/* ============================== 实例生命周期 ============================== */

/*
 * 创建虚拟实例：从 iot_mem 堆分配行缓冲 / 历史 / 粘贴缓冲 / JOB 表 /
 * 变量表（大小由 shell_config.h 决定）。write 为 NULL 时丢弃输出。
 * 返回实例指针；参数非法或内存不足返回 NULL。
 */
shell_t *shell_instance_create(const shell_config_t *cfg);

/* 销毁实例并释放全部堆内存（实例内的任务应先停止） */
void shell_instance_destroy(shell_t *sh);

/* 复位实例状态（清空变量 / JOB / 行与粘贴缓冲），保留配置 */
void shell_instance_reset(shell_t *sh);

/* ============================== 实例输入 / 执行 ============================== */

int shell_instance_feed(shell_t *sh, const char *data, uint32_t len);
uint32_t shell_instance_pending(shell_t *sh);
int shell_instance_exec_line(shell_t *sh, const char *line);
int shell_instance_exec_script(shell_t *sh, const char *text);

/*
 * 为实例创建命令输入任务（从 shell_instance_feed 缓冲读取并交互）。
 * 通常只给默认/串口实例调用；虚拟实例一般同步执行，不需要输入任务。
 * 任务名为 "shell"，重复启动同一实例会复用已有任务。
 */
int shell_instance_start(shell_t *sh);

/*
 * 在临时虚拟实例中执行脚本：新实例拥有独立变量与 JOB 表，输出转发到
 * parent 的 write 后端，执行完自动销毁。`run` 命令即此路径。
 * 返回 0 / 负错误码。
 */
int shell_exec_isolated(shell_t *parent, const char *text);

/* ============================== 默认实例 ============================== */

/*
 * 初始化默认实例（平台绑定使用）：首个调用创建，之后复用并复位。
 * 返回 0 成功；-1 参数非法或内存不足。
 * 未初始化时 shell_exec_line() 返回 -SHELL_ERR_STATE，shell_feed() 返回 0。
 */
int shell_setup(const shell_config_t *cfg);

/* 默认实例指针（未初始化返回 NULL） */
shell_t *shell_default(void);

void shell_reset(void);
int shell_feed(const char *data, uint32_t len);
uint32_t shell_pending(void);
int shell_exec_line(const char *line);
int shell_exec_script(const char *text);
int shell_start(void);

/*
 * 平台默认绑定并启动 Shell：
 *   STM32 : 输入 / 输出绑定 UART1（hal_uart_t instance=1 约定）
 *   PC    : 输入 / 输出绑定标准输入输出
 * 内部完成 shell_setup() + 输入泵任务 + shell_start()。
 * 返回 0 成功；平台不支持或资源不足返回 -1。
 */
int shell_platform_start(const char *version);

/* ============================== 输出（实例级） ============================== */

void shell_printf(shell_t *sh, const char *fmt, ...);
void shell_puts(shell_t *sh, const char *s);
void shell_putchar(shell_t *sh, char c);
void shell_printf_job(shell_t *sh, const char *job, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* SHELL_H */
