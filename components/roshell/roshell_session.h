#ifndef RS_SESSION_H
#define RS_SESSION_H

/*
 * roshell_session.h - roshell 交互会话内部声明
 *
 * 会话层与内建命令共享的错误输出、Ctrl-C 轮询、会话任务与版本串访问。
 * 实现见 roshell_session.c；会话对外 API 在 roshell.h。
 * 仅供 roshell 各实现文件使用，平台 / 组件代码不应依赖。
 */

#include "roshell.h"

/* 格式化错误输出：ERR <code>: <msg> */
void rs_error(int code, const char *fmt, ...);

/* 等待期间消费输入：只识别 Ctrl-C，其余字节丢弃 */
bool rs_session_pump_ctrlc(void);

/* 会话输入任务（未启动为 NULL） */
struct ros_task *rs_session_task(void);

/* 会话启动时登记的版本串，供 ver 命令显示，可为 NULL */
const char *rs_session_version(void);

#endif /* RS_SESSION_H */
