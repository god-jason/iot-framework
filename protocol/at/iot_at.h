#ifndef IOT_AT_H
#define IOT_AT_H

/*
 * AT 指令事务引擎（事件驱动）。
 * 依赖：UART Port（发送/接收）+ iot_at_parser（行/提示符）+ iot_at_urc（URC 分发）。
 * 约束：同一时刻仅允许一条普通命令处于等待响应状态；URC 单独分发。
 * 说明：iot_at_process() 内部会从 UART 泵取接收数据并处理超时，主循环周期性调用即可。
 */

#include "iot_types.h"
#include "iot_port.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    IOT_AT_IDLE,
    IOT_AT_SENDING,
    IOT_AT_WAITING,
    IOT_AT_DATA,
    IOT_AT_COMPLETE,
    IOT_AT_ERROR
} iot_at_state_t;

typedef enum {
    IOT_AT_RESULT_OK,
    IOT_AT_RESULT_ERROR,
    IOT_AT_RESULT_TIMEOUT,
    IOT_AT_RESULT_BUSY,
    IOT_AT_RESULT_CANCELLED,
    IOT_AT_RESULT_OVERFLOW,
    IOT_AT_RESULT_IO_ERROR
} iot_at_result_t;

typedef void (*iot_at_callback_fn)(iot_at_result_t result,
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
    iot_at_callback_fn callback;
    void *user;
} iot_at_request_t;

typedef struct {
    const iot_port_uart_t *uart;
    uint8_t *rx_buffer;
    uint32_t rx_buffer_size;
    char *line_buffer;
    uint32_t line_buffer_size;
    uint32_t default_timeout;
    uint8_t default_retry;
    uint8_t echo_enabled;
} iot_at_config_t;

int iot_at_init(const iot_at_config_t *config);
int iot_at_start(void);
int iot_at_stop(void);
int iot_at_deinit(void);

int iot_at_send(const char *command);
int iot_at_send_raw(const uint8_t *data, uint32_t len);

/* 阻塞式执行一条命令，默认等待 "OK"。返回执行结果。 */
iot_at_result_t iot_at_exec(const char *command, uint32_t timeout);

/* 阻塞式完成一个事务（command 可为 NULL，表示仅等待响应）。返回执行结果。 */
iot_at_result_t iot_at_transact(const iot_at_request_t *request);

/* 非阻塞提交一个事务，结果通过 callback 通知。返回 IOT_OK / IOT_ERR_BUSY。 */
int iot_at_query(const iot_at_request_t *request);

int iot_at_cancel(void);

void iot_at_input(const uint8_t *data, uint32_t len);
int iot_at_process(void);

bool iot_at_is_busy(void);
iot_at_state_t iot_at_get_state(void);
const char *iot_at_get_response(void);

#ifdef __cplusplus
}
#endif

#endif /* IOT_AT_H */