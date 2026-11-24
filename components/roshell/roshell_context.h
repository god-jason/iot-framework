#ifndef RS_CONTEXT_H
#define RS_CONTEXT_H

/*
 * roshell_context.h - roshell 会话上下文内部声明
 *
 * 会话上下文（rs_context_t）、会话层与内建命令共享的错误输出、Ctrl-C 轮询、
 * 会话任务与版本串访问。实现见 roshell_context.c；会话对外 API 在 roshell.h。
 * 仅供 roshell 各实现文件使用，平台 / 组件代码不应依赖。
 */

#include "roshell.h"
#include "ringbuf.h"
#include "ros.h"

#ifndef RS_SHELL_RX_MAX
#define RS_SHELL_RX_MAX        256U    /* 接收环形缓冲字节数 */
#endif

#ifndef RS_CWD_MAX
#define RS_CWD_MAX             128U    /* 会话当前工作目录缓冲长度 */
#endif

/* 会话上下文（单实例，rs_shell_start 时挂到 rs->ctx） */
typedef struct rs_context {
    rs_t             *rs;                 /* 归属实例 */
    ros_task_t       *task;               /* 输入任务 */
    const char       *version;
    ringbuf_t         rx;
    uint8_t           rx_mem[RS_SHELL_RX_MAX];
    char              line[RS_LINE_MAX];
    char              cwd[RS_CWD_MAX];    /* 当前工作目录，默认 "/" */
    uint32_t          line_len;
    bool              last_cr;
    bool              last_esc;
    vec_t             waits;              /* 可暂停 wait 登记表（元素类型见内建命令，惰性初始化） */
    volatile bool     started;
    volatile bool     finished;
    volatile bool     exit_req;
} rs_context_t;

/* 格式化错误输出：ERR <code>: <msg> */
void rs_error(int code, const char *fmt, ...);

/* 等待期间消费输入：只识别 Ctrl-C，其余字节丢弃 */
bool rs_session_pump_ctrlc(void);

/* 会话输入任务（未启动为 NULL） */
struct ros_task *rs_session_task(void);

/* 会话启动时登记的版本串，供 ver 命令显示，可为 NULL */
const char *rs_session_version(void);

/* 可暂停 wait 登记表（会话级 vec，元素类型由内建命令定义，首次使用时初始化） */
vec_t *rs_session_waits(void);

#endif /* RS_CONTEXT_H */
