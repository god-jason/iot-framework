#ifndef RS_IO_H
#define RS_IO_H

/*
 * roshell_io.h - 把 roshell 会话接到 ros_io_t 字节流上
 *
 * 与 roshell_repl（rs_shell_*）的分工：
 *   roshell_io   面向“流”：tty / UART / 管道 / socket 只要实现 ros_io_t 的
 *                read/write 就能驱动会话。输入靠轮询（rs_repl_poll）或
 *                阻塞循环（rs_repl_run），不强制创建任务，也不依赖环形缓冲。
 *   roshell_repl 面向“会话外壳”：保留 rs_shell_* 旧接口（环形缓冲 + 输入任务 +
 *               口令/回收），内部同样走本模块的行编辑与分派实现。
 *
 * 行编辑与 roshell_repl 完全一致：回显、退格、CRLF 归一、Ctrl-C 清行、
 * ESC 序列吞掉、超 RS_LINE_MAX 截断；无历史、无 Tab 补全。
 */

#include "roshell.h"
#include "ros_io.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 绑定输入/输出流并启动会话：初始化行编辑状态、命令 hook（rs_cmd_install）、
 * cwd（重置为 "/"），随后打印 banner 与提示符。
 *   in  : 输入流；传 NULL 表示只用手工投喂（rs_repl_feed / rs_shell_feed）
 *   out : 输出流；传 NULL 表示使用当前任务 stdio（ros_io_write(NULL, ...)）
 * 返回 RS_OK；参数非法返回 RS_ERR_PARAM。实例由调用方用 rs_create() 创建。
 */
int rs_repl_open(roshell_t *rs, ros_io_t *in, ros_io_t *out, const char *version);

/* 从输入流读取当前可用字节并处理（不阻塞），返回提交的行数；
 * 流结束（read < 0）时置 EOF 并请求会话退出。 */
uint32_t rs_repl_poll(roshell_t *rs);

/* 手工投喂字节（中断 / 其它任务 / 测试），返回提交的行数 */
uint32_t rs_repl_feed(roshell_t *rs, const char *data, uint32_t len);

/* 阻塞循环：poll + 让出 CPU，直到 exit 命令或流 EOF；返回 RS_OK */
int rs_repl_run(roshell_t *rs);

/* 替换输出流（NULL = 回到当前任务 stdio）；用于切换串口 / 重定向 */
void rs_repl_set_out(roshell_t *rs, ros_io_t *out);
ros_io_t *rs_repl_out(const roshell_t *rs);

/* 输入流是否已结束；未提交编辑行的字节数 */
bool rs_repl_eof(const roshell_t *rs);
uint32_t rs_repl_line_pending(const roshell_t *rs);

#ifdef __cplusplus
}
#endif

#endif /* RS_IO_H */
