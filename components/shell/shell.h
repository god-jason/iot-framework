#ifndef SHELL_H
#define SHELL_H

/*
 * shell.h - IoT Shell V1.1 公共接口
 *
 * 形态：运行在 RTOS 之上的交互式命令行，输入来自注入的字节流，
 * 输出经 shell_config_t.write 后端（串口 / RTT / 测试捕获）。
 *
 * 能力：
 *   - SHELL_CTX：内建命令（help/ver/echo/print/set/unset/wait/ps/kill/
 *     pause/resume/load/loads/unload/run/start/stop/fg/jobs/job/waitjob/state）
 *   - VM_CTX：脚本语句（SET/PRINT/IF/FOR/WHILE/LOOP/BREAK/CONTINUE/
 *     WAIT TIME/START JOB/STOP JOB/ON ERROR/ON STOP/ENDJOB/LABEL/GOTO/CALL/RETURN/
 *     SHELL PRINT|LOG|STATE|PS）
 *   - 每个 JOB 由独立 RTOS 任务承载；shell_exec_script() 提供不建任务的同步执行，
 *     供单元测试与调试使用。
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
    SHELL_ERR_DANGER     = 1016,   /* 需要 confirm */
    SHELL_ERR_RESOURCE   = 1017,   /* 资源耗尽 */
    SHELL_ERR_NAME       = 1018,   /* 名称冲突 / 保留名 */
    SHELL_ERR_STATE      = 1019,   /* 当前状态不允许 */
    SHELL_ERR_JUMP       = 1020    /* 非法跳转 / 环形等待 */
};

/* 扩展命令返回值：0 成功，-<code> 失败 */
#define SHELL_CMD_OK        0
#define SHELL_CMD_ERR(code) (-(code))

/* ============================== 扩展命令 ============================== */

typedef enum {
    CMD_NONE      = 0,
    CMD_DANGER    = (1U << 0),   /* 执行前要求 confirm */
    CMD_HIDDEN    = (1U << 1),   /* help 不列出 */
    CMD_REENTRANT = (1U << 2)    /* 允许被 SHELL <cmd> 透传 */
} shell_cmd_flags_t;

typedef int (*shell_cmd_fn)(int argc, char **argv);

typedef struct shell_cmd {
    const char *name;
    shell_cmd_fn handler;
    const char *usage;
    const char *help;
    const char *group;
    uint32_t    flags;
} shell_cmd_t;

/* 注册扩展命令（显式调用；重复名/与基础命令同名会被拒绝） */
int shell_register_cmd(const shell_cmd_t *cmd);

/* ============================== 生命周期 ============================== */

typedef struct {
    const char *version;                              /* 版本字符串 */
    int (*write)(const char *data, uint32_t len, void *user);
    void *user;
    void (*reset)(void);                              /* reboot 钩子，可空 */
} shell_config_t;

/*
 * 初始化 Shell：清空变量表 / 命令表 / JOB 表，保存输出后端。
 * 返回 0 成功；-1 参数非法。
 * 命名说明：console 组件的旧 shell 已占用 shell_init/shell_input，
 * 本模块使用 shell_setup/shell_feed 作为入口。
 */
int shell_setup(const shell_config_t *cfg);

/* 复位 Shell 状态（测试用） */
void shell_reset(void);

/* ============================== 输入 ============================== */

/*
 * 注入输入字节（可在任务或 ISR 中调用，内部环形缓冲）。
 * 返回写入字节数；缓冲满丢弃多余字节并返回实际写入数。
 */
int shell_feed(const char *data, uint32_t len);

/* 尚未被 Shell 任务取走的输入字节数（输入泵据此做流控） */
uint32_t shell_pending(void);

/*
 * 同步执行一行（SHELL_CTX）：命令优先，其次是脚本语句（创建匿名 JOB
 * 并同步执行）。返回 0 成功，负值为错误码（-1001 …）。
 */
int shell_exec_line(const char *line);

/*
 * 同步执行一段脚本（不创建 RTOS 任务，便于测试/调试）。
 * 输出带 [JOB @interactive] 前缀。返回 0 / 负错误码。
 */
int shell_exec_script(const char *text);

/* 创建 Shell 任务（从 shell_feed() 缓冲读取并交互）；返回 0 / -1 */
int shell_start(void);

/*
 * 平台默认绑定并启动 Shell：
 *   STM32 : 输入 / 输出绑定 UART1（hal_uart_t instance=1 约定）
 *   PC    : 输入 / 输出绑定标准输入输出
 * 内部完成 shell_setup() + 输入泵任务 + shell_start()。
 * 返回 0 成功；平台不支持或资源不足返回 -1。
 */
int shell_platform_start(const char *version);

/* ============================== 输出 ============================== */

void shell_printf(const char *fmt, ...);
void shell_puts(const char *s);
void shell_putchar(char c);
void shell_printf_job(const char *job, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* SHELL_H */
