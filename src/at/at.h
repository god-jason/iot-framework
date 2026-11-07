#ifndef AT_H
#define AT_H

/*
 * AT 指令事务引擎（事件驱动）。
 * 依赖：UART Port（发送/接收）+ at_parser（行/提示符）+ at_urc（URC 分发）。
 * 约束：同一时刻仅允许一条普通命令处于等待响应状态；URC 单独分发。
 * 说明：at_process() 内部会从 UART 泵取接收数据并处理超时，主循环周期性调用即可。
 */

#include "iot_types.h"
#include "port.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AT_IDLE,
    AT_SENDING,
    AT_WAITING,
    AT_DATA,
    AT_COMPLETE,
    AT_ERROR
} at_state_t;

typedef enum {
    AT_RESULT_OK,
    AT_RESULT_ERROR,
    AT_RESULT_TIMEOUT,
    AT_RESULT_BUSY,
    AT_RESULT_CANCELLED,
    AT_RESULT_OVERFLOW,
    AT_RESULT_IO_ERROR
} at_result_t;

typedef void (*at_callback_fn)(at_result_t result,
                                   const char *response,
                                   uint32_t len,
                                   void *user);

typedef struct {
    const char *command;
    const char *expect;   /* 可选：数据行关键字过滤 */
    const char *success;  /* 成功终止符，NULL=默认 "OK" */
    const char *failure;  /* 失败终止符，NULL=默认 "ERROR" */
    uint32_t timeout;
    uint8_t retry;
    at_callback_fn callback;
    void *user;
} at_request_t;

typedef struct {
    port_uart_handle_t *uart;
    uint8_t *rx_buffer;
    uint32_t rx_buffer_size;
    char *line_buffer;
    uint32_t line_buffer_size;
    uint32_t default_timeout;
    uint8_t default_retry;
    uint8_t echo_enabled;
} at_config_t;

int at_init(const at_config_t *config);
int at_start(void);
int at_stop(void);
int at_deinit(void);

int at_send(const char *command);
int at_send_raw(const uint8_t *data, uint32_t len);

/* 阻塞式执行一条命令，默认等待 "OK"。返回执行结果。 */
at_result_t at_exec(const char *command, uint32_t timeout);

/* 阻塞式完成一个事务（command 可为 NULL，表示仅等待响应）。返回执行结果。 */
at_result_t at_transact(const at_request_t *request);

/* 非阻塞提交一个事务，结果通过 callback 通知。返回 IOT_OK / -1。 */
int at_query(const at_request_t *request);

int at_cancel(void);

void at_input(const uint8_t *data, uint32_t len);
int at_process(void);

bool at_is_busy(void);
at_state_t at_get_state(void);
const char *at_get_response(void);

#ifdef __cplusplus
}
#endif

#endif /* AT_H */