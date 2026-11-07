#ifndef PORT_UART_H
#define PORT_UART_H

#include "port_def.h"
#include "port_dma.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PORT_UART_WORD_7BIT = 7,
    PORT_UART_WORD_8BIT = 8,
    PORT_UART_WORD_9BIT = 9
} port_uart_word_t;

typedef enum {
    PORT_UART_PARITY_NONE = 0,
    PORT_UART_PARITY_EVEN,
    PORT_UART_PARITY_ODD
} port_uart_parity_t;

typedef enum {
    PORT_UART_STOP_1 = 0,
    PORT_UART_STOP_2
} port_uart_stop_t;

typedef enum {
    PORT_UART_FLOW_NONE = 0,
    PORT_UART_FLOW_RTS,
    PORT_UART_FLOW_CTS,
    PORT_UART_FLOW_RTS_CTS
} port_uart_flow_t;

typedef enum {
    PORT_UART_MODE_TX = 1,
    PORT_UART_MODE_RX = 2,
    PORT_UART_MODE_TX_RX = 3
} port_uart_mode_t;

typedef struct {
    uint32_t baudrate;

    port_uart_word_t word_length;
    port_uart_parity_t parity;
    port_uart_stop_t stop_bits;

    port_uart_flow_t flow_control;
    port_uart_mode_t mode;

    bool oversampling;
} port_uart_config_t;

typedef enum {
    PORT_UART_EVENT_TX_COMPLETE = 0,
    PORT_UART_EVENT_RX_COMPLETE,
    PORT_UART_EVENT_RX_HALF,
    PORT_UART_EVENT_RX_IDLE,
    PORT_UART_EVENT_ERROR,
    PORT_UART_EVENT_ABORT
} port_uart_event_t;

typedef void (*port_uart_callback_t)(void *handle, port_uart_event_t event,
                                     size_t length, void *user_data);

typedef struct port_uart_handle {
    port_handle_base_t base;

    port_uart_config_t config;

    port_dma_handle_t *tx_dma;
    port_dma_handle_t *rx_dma;

    port_uart_callback_t callback;
    void *user_data;
} port_uart_handle_t;

port_status_t port_uart_init(port_uart_handle_t *handle, const port_uart_config_t *config);
port_status_t port_uart_deinit(port_uart_handle_t *handle);

/* 阻塞收发 */
port_status_t port_uart_transmit(port_uart_handle_t *handle, const uint8_t *data,
                                 size_t length, uint32_t timeout);
port_status_t port_uart_receive(port_uart_handle_t *handle, uint8_t *data,
                                size_t length, uint32_t timeout);

/* 中断收发 */
port_status_t port_uart_transmit_it(port_uart_handle_t *handle, const uint8_t *data, size_t length);
port_status_t port_uart_receive_it(port_uart_handle_t *handle, uint8_t *data, size_t length);

/* DMA 收发 */
port_status_t port_uart_transmit_dma(port_uart_handle_t *handle, const uint8_t *data, size_t length);
port_status_t port_uart_receive_dma(port_uart_handle_t *handle, uint8_t *data, size_t length);

/* 接收至空闲 */
port_status_t port_uart_receive_to_idle(port_uart_handle_t *handle, uint8_t *data,
                                        size_t length, port_transfer_mode_t mode);

/* 获取接收长度 */
size_t port_uart_get_rx_count(port_uart_handle_t *handle);

/* 中止 */
port_status_t port_uart_abort_tx(port_uart_handle_t *handle);
port_status_t port_uart_abort_rx(port_uart_handle_t *handle);
port_status_t port_uart_abort(port_uart_handle_t *handle);

/* 状态 */
port_state_t port_uart_get_state(port_uart_handle_t *handle);
port_status_t port_uart_get_error(port_uart_handle_t *handle);

/* 回调 */
port_status_t port_uart_register_callback(port_uart_handle_t *handle,
                                          port_uart_callback_t callback, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* PORT_UART_H */
