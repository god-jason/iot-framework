#ifndef ROSHELL_CMD_H
#define ROSHELL_CMD_H

/*
 * roshell_cmd.h - roshell 命令集与交互会话
 *
 * 形态：在 roshell 脚本引擎之上提供一组小写、大小写敏感的内建命令
 * （help/ver/echo/ps/kill/pause/resume/mem/state/jobs/waitjob/exit），
 * 并把串口（STM32）或标准输入输出（PC）的字节流组装成行驱动会话。
 *
 * 分派规则（roshell_cmd_dispatch）：
 *   1. 行首 token 命中内建命令表 -> 执行命令；
 *   2. 否则查 cmd 全局命令表（components/cmd，设备/组件命令走这里）；
 *   3. 仍未命中 -> 整行交给 roshell 脚本引擎（roshell_exec_line）。
 *
 * 会话生命周期（对应旧 shell 的用法，平台层只依赖本头文件）：
 *   roshell_shell_start()   创建实例与输入任务，打印 banner 与提示符；
 *   roshell_shell_feed()    串口 ISR / 轮询投喂字节（进接收环形缓冲）；
 *   roshell_shell_process() 消费缓冲：组行 + 分派（输入任务内部调用，
 *                           无输入任务的平台 / 测试也可直接驱动）；
 *   roshell_shell_started() / roshell_shell_task_finished() 状态查询；
 *   roshell_shell_shutdown() 输入任务退出后回收任务与实例内存。
 *
 * 行编辑大幅简化：回显、退格、CRLF 归一、Ctrl-C 清行、ESC 序列吞掉；
 * 无历史与 Tab 补全。
 */

#include <stdint.h>
#include <stdbool.h>

#include "roshell.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================== 命令分派 ============================== */

/*
 * 执行一行输入：内建命令 -> cmd 全局命令表 -> roshell 脚本引擎。
 * 返回 0 成功；负值为 RO_ERR_* 或命令错误码。rs 可为 NULL
 * （仅脚本路径与需要实例的命令返回 -RO_ERR_PARAM）。
 */
int roshell_cmd_dispatch(roshell_t *rs, const char *line);

/* ============================== 交互会话 ============================== */

/*
 * 创建 roshell 实例与输入任务（任务名 "roshell_io"），打印 banner 与
 * 提示符。version 仅用于 ver 命令显示，可为 NULL。
 * 幂等：已启动直接返回 0；内存不足返回 -1。
 */
int roshell_shell_start(const char *version);

/* 投喂输入字节（串口 ISR / 轮询可调用）：追加到接收环形缓冲；
 * 会话未启动或缓冲满时丢弃。 */
void roshell_shell_feed(const char *data, uint32_t len);

/* 消费接收缓冲中的全部字节：组行并分派，返回提交的行数（回车计数）。
 * 输入任务循环调用；测试或无任务平台可手动驱动。 */
uint32_t roshell_shell_process(void);

bool roshell_shell_started(void);
bool roshell_shell_task_finished(void);

/* 请求关闭会话（exit 命令 / 平台输入 EOF）：输入任务在本轮后退出 */
void roshell_shell_request_exit(void);

/*
 * 回收会话：要求 roshell_shell_task_finished() 为真。
 * 销毁输入任务与 roshell 实例，回到零占用状态。返回 0 / -1。
 */
int roshell_shell_shutdown(void);

/* 会话 roshell 实例（未启动返回 NULL），供上位机 / 测试观察 */
roshell_t *roshell_shell_instance(void);

/* 当前编辑行已积累的字节数（未提交部分），供测试观察截断行为 */
uint32_t roshell_shell_line_pending(void);

/* ============================== 平台默认绑定 ============================== */

/*
 * 平台层实现（platforms/pc/pc_shell.c、platforms/stm32/stm32_shell.c）：
 *   PC    : stdin 逐字节 feed 到会话；上电只跑监听任务，
 *           输入口令 "hello" 后才创建会话；exit 回收后回到监听；
 *           监听态 quit / 管道 EOF 停止内核。
 *   STM32 : UART1 轮询逐字节 feed；同样口令 "hello" 开启会话。
 * 返回 0 成功；平台不支持或资源不足返回 -1。
 */
int roshell_platform_start(const char *version);

#ifdef __cplusplus
}
#endif

#endif /* ROSHELL_CMD_H */
