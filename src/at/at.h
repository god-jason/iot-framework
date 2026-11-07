#ifndef AT_H
#define AT_H

/*
 * AT 指令事务引擎（事件驱动）。
 * 依赖：UART Port（发送/接收）+ at_parser（行/提示符）+ at_urc（URC 分发）。
 * 约束：同一时刻仅允许一条普通命令处于等待响应状态；URC 单独分发。
 * 说明：at_process() 内部会从 UART 泵取接收数据并处理超时，主循环周期性调用即可。
 *
 * 模块说明：
 *   - 单例实现，配置在 at_init() 时整体拷贝；
 *   - 支持阻塞（at_exec/at_transact）与非阻塞（at_query + 回调）两种用法；
 *   - 超时与重试的计时基于 at_process() 调用次数，而非真实毫秒；
 *   - at_process() 仅在 at_start() 之后才主动读取 UART。
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "port.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 事务状态机状态；AT_DATA 为预留状态，当前实现未使用 */
typedef enum {
    AT_IDLE,      /* 空闲，无事务 */
    AT_SENDING,   /* 正在发送命令 */
    AT_WAITING,   /* 已发送，等待响应/超时 */
    AT_DATA,      /* 数据模式（预留） */
    AT_COMPLETE,  /* 事务成功结束 */
    AT_ERROR      /* 事务失败结束 */
} at_state_t;

/* 事务结果码；AT_RESULT_IO_ERROR 为预留码，当前实现未返回 */
typedef enum {
    AT_RESULT_OK,         /* 收到成功终止符 */
    AT_RESULT_ERROR,      /* 收到失败终止符 */
    AT_RESULT_TIMEOUT,    /* 超时且重试耗尽 */
    AT_RESULT_BUSY,       /* 提交失败：已有事务在途 */
    AT_RESULT_CANCELLED,  /* 被 at_cancel() 取消 */
    AT_RESULT_OVERFLOW,   /* 行缓冲溢出 */
    AT_RESULT_IO_ERROR    /* IO 错误（预留） */
} at_result_t;

/*
 * 事务完成回调。
 * 参数：result 事务结果；response 累积的响应文本；len 文本长度；
 *       user 发起事务时传入的用户指针。
 */
typedef void (*at_callback_fn)(at_result_t result,
                                   const char *response,
                                   uint32_t len,
                                   void *user);

/* 单条 AT 事务的请求参数 */
typedef struct {
    const char *command;   /* 要发送的命令，NULL 表示只等待响应 */
    const char *expect;    /* 可选：数据行关键字过滤，NULL 表示不过滤 */
    const char *success;   /* 成功终止符，NULL=默认 "OK" */
    const char *failure;   /* 失败终止符，NULL=默认 "ERROR" */
    uint32_t timeout;      /* 超时（at_process 调用次数），0 表示用默认值 */
    uint8_t retry;         /* 超时后的重发次数 */
    at_callback_fn callback;   /* 结果回调，可为 NULL（阻塞模式） */
    void *user;            /* 透传给回调的用户指针 */
} at_request_t;

/* AT 引擎配置（at_init 时整体拷贝，缓冲区仍由调用者持有） */
typedef struct {
    port_uart_handle_t *uart;   /* 绑定的 UART 句柄，必须非 NULL */
    uint8_t *rx_buffer;         /* 接收泵取缓冲，可为 NULL（不主动收） */
    uint32_t rx_buffer_size;    /* 接收缓冲字节数 */
    char *line_buffer;          /* 行解析缓冲，可为 NULL（不初始化解析器） */
    uint32_t line_buffer_size;  /* 行缓冲字节数 */
    uint32_t default_timeout;   /* 默认超时（at_process 调用次数） */
    uint8_t default_retry;      /* 默认重试次数（预留，当前以请求级 retry 为准） */
    uint8_t echo_enabled;       /* 回显开关（预留，当前实现未使用） */
} at_config_t;

/*
 * 初始化 AT 引擎。
 * 参数：config 配置指针（uart 必须非 NULL）。
 * 返回：0 成功；参数非法返回 -1。
 */
int at_init(const at_config_t *config);

/* 启动引擎（开始从 UART 泵取数据）：成功返回 0，未初始化返回 -1 */
int at_start(void);

/* 停止引擎（不再泵取数据，清空在途事务标志）：恒返回 0 */
int at_stop(void);

/* 反初始化（清标志与事务状态，不释放缓冲）：恒返回 0 */
int at_deinit(void);

/* 仅发送命令（自动追加 "\r\n"），不建立事务：成功 0，失败 -1 */
int at_send(const char *command);

/* 发送原始字节（不追加换行）：成功 0，参数非法 -1 */
int at_send_raw(const uint8_t *data, uint32_t len);

/* 阻塞式执行一条命令，默认等待 "OK"。返回执行结果。 */
at_result_t at_exec(const char *command, uint32_t timeout);

/* 阻塞式完成一个事务（command 可为 NULL，表示仅等待响应）。返回执行结果。 */
at_result_t at_transact(const at_request_t *request);

/* 非阻塞提交一个事务，结果通过 callback 通知。返回 0 / -1。 */
int at_query(const at_request_t *request);

/* 取消在途事务：成功 0，无在途事务返回 -1 */
int at_cancel(void);

/* 向解析器注入接收数据（可用于中断/DMA 回调直接喂数据） */
void at_input(const uint8_t *data, uint32_t len);

/* 主循环轮询：泵取 UART 数据并处理超时/重试，恒返回 0 */
int at_process(void);

/* 是否有事务在途 */
bool at_is_busy(void);

/* 获取当前状态机状态 */
at_state_t at_get_state(void);

/* 获取最近一次事务的响应文本（内部静态缓冲，勿修改/释放） */
const char *at_get_response(void);

#ifdef __cplusplus
}
#endif

#endif /* AT_H */