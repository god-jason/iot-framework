#ifndef RS_REPL_H
#define RS_REPL_H

/*
 * roshell_repl.h - 交互式会话外壳（REPL）：环形缓冲 + 输入任务 + 会话生命周期
 *
 * 分层：
 *   roshell.h        引擎本体（rs_create / rs_exec / rs_exec_line），不含交互；
 *   roshell_io.h     字节流 <-> 行编辑/分派（ros_io_t 输入输出，本文件的实现基础）；
 *   roshell_repl.h   本文件：会话外壳（feed/process/任务/口令/回收/cwd/exit）。
 *
 * 会话生命周期（每个会话一个句柄 roshell_t*，平台层只依赖本头文件）：
 *   rs_shell_start()   新建独立会话与输入任务，返回句柄，打印 banner 与提示符；
 *   rs_shell_feed()    串口 ISR / 轮询投喂字节（进该会话的接收环形缓冲）；
 *   rs_shell_process() 消费缓冲：组行 + 分派（输入任务内部调用，
 *                           无输入任务的平台 / 测试也可直接驱动）；
 *   rs_shell_started() / rs_shell_task_finished() 状态查询；
 *   rs_shell_shutdown() 输入任务退出后回收任务与会话内存。
 *
 * 行编辑（实现在 roshell_io.c）：回显、退格、CRLF 归一、Ctrl-C 清行、
 * ESC 序列吞掉；无历史与 Tab 补全。
 */

#include "roshell.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 新建并启动一个会话：分配独立的会话上下文（接收缓冲 / 行编辑状态 / cwd）
 * 并创建输入任务（任务名 "roshell_io"），打印 banner 与提示符。每次调用
 * 产生一个独立会话，可同时存在多个；返回的实例即后续会话 API 的句柄。
 * version 仅用于 ver 命令显示，可为 NULL。内存不足返回 NULL。
 */
roshell_t *rs_shell_start(const char *version);

/* 投喂输入字节（串口 ISR / 轮询可调用）：追加到该会话的接收环形缓冲；
 * rs 为 NULL 或缓冲满时丢弃。 */
void rs_shell_feed(roshell_t *rs, const char *data, uint32_t len);

/* 消费接收缓冲中的全部字节：组行并分派，返回提交的行数（回车计数）。
 * 输入任务循环调用；测试或无任务平台可手动驱动。 */
uint32_t rs_shell_process(roshell_t *rs);

bool rs_shell_started(const roshell_t *rs);
bool rs_shell_task_finished(const roshell_t *rs);

/* 会话当前工作目录（默认 "/"，会话启动时重置）；用于 pwd/cd 等命令 */
const char *rs_shell_cwd(const roshell_t *rs);
int rs_shell_cwd_set(roshell_t *rs, const char *path);

/* 请求关闭会话（exit 命令 / 平台输入 EOF）：输入任务在本轮后退出 */
void rs_shell_request_exit(roshell_t *rs);

/*
 * 回收会话：要求 rs_shell_task_finished(rs) 为真。
 * 销毁输入任务、会话上下文与 roshell 实例。返回 0 / -1。
 */
int rs_shell_shutdown(roshell_t *rs);

/* 当前编辑行已积累的字节数（未提交部分），供测试观察截断行为 */
uint32_t rs_shell_line_pending(const roshell_t *rs);

/* ============================== 平台默认绑定 ============================== */

/*
 * 平台层弱默认实现（platforms/pc/pc_shell.c、platforms/stm32/stm32_shell.c），
 * 应用可提供强定义覆盖（如 apps/shell/app_shell.c、apps/f103cb/app_shell.c）：
 *   PC    : stdin 逐字节 feed 到会话；上电只跑监听任务，
 *           输入口令 "hello" 后才创建会话；exit 回收后回到监听；
 *           监听态 quit / 管道 EOF 停止内核。
 *   STM32 : UART1 轮询逐字节 feed；同样口令 "hello" 开启会话。
 * 返回 0 成功；平台不支持或资源不足返回 -1。
 */
int rs_platform_start(const char *version);

#ifdef __cplusplus
}
#endif

#endif /* RS_REPL_H */
