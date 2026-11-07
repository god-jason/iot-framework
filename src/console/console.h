/*
 * console.h - 主串口（UART1）控制台与命令行 Shell 接口
 *
 * 模块提供：
 *  - 控制台输出：直接向 UART 写一行（自动追加 CRLF）；
 *  - 控制台输入：主循环泵取 UART 接收数据并交给 Shell 解析；
 *  - 命令管理：注册/注销命令，按空格切分 argv 后同步回调执行。
 *
 * 实现为静态单实例，命令表 16 条、单行最长 128 字节，无动态内存。
 */

#ifndef CONSOLE_H
#define CONSOLE_H

/*
 * 主串口（UART1）控制台（含命令行 Shell）：
 *  - 默认输出日志（注册 logger 后端 -> UART）
 *  - 接受命令输入（UART 接收 -> shell 分发）
 *  - 命令注册/执行（按空格切分 argv）
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "port.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 控制台配置 */
typedef struct {
    port_uart_handle_t *uart;   /* 绑定的 UART 句柄，不可为 NULL */
    uint8_t *rx_buffer;         /* 接收泵取用的临时缓冲（可由调用者提供） */
    uint32_t rx_buffer_size;    /* 接收缓冲容量（字节），为 0 则不消费数据 */
} console_config_t;

/*
 * 初始化控制台并初始化 Shell。
 * 参数：config - 配置，见 console_config_t
 * 返回：0 成功；-1 参数非法（config 或 uart 为 NULL）
 */
int console_init(const console_config_t *config);

/*
 * 启动控制台，输出就绪提示行。
 * 返回：PORT_OK；依赖 console_init() 已成功调用
 */
int console_start(void);

/* 主循环调用：泵取 UART 接收字节并喂给 shell */
/* 返回：PORT_OK 正常；-1 控制台未初始化 */
int console_process(void);

/* 直接输出一行（绕过 logger，用于控制台回显/提示） */
/* 返回：PORT_OK 成功；-1 未初始化或参数非法 */
int console_puts(const char *s);

/* ---------------- Shell ---------------- */
/*
 * 命令回调。
 * 参数：argc - 参数个数（>= 1，argv[0] 为命令名）；argv - 参数数组
 * 返回：命令自定义状态码，由 shell_execute() 原样返回
 */
typedef int (*shell_command_fn)(int argc, char **argv);

/* Shell 命令描述：name 与 callback 必须非 NULL，字符串需长期有效 */
typedef struct {
    const char *name;          /* 命令名（用于匹配 argv[0]） */
    const char *description;   /* 命令说明（可为 NULL） */
    shell_command_fn callback; /* 命令处理函数 */
} shell_command_t;

/* 清空命令表与行缓冲；返回 0 */
int shell_init(void);

/* 注册命令；返回 0 成功，-1 参数非法或命令表已满（16 条） */
int shell_register(const shell_command_t *command);
/* 按名称注销命令；返回 0 成功，-1 参数非法或未找到 */
int shell_unregister(const char *name);

/* 喂入字节流（行以 \n 结束即执行）；返回 0，-1 参数非法 */
int shell_input(const char *data, uint32_t len);
/* 预留的周期处理钩子，当前直接返回 0 */
int shell_process(void);

/* 直接执行一行命令；返回命令回调的返回值，-1 参数非法或命令未注册 */
int shell_execute(const char *command);

#ifdef __cplusplus
}
#endif

#endif /* CONSOLE_H */
