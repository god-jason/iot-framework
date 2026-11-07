#ifndef IOT_UART_H
#define IOT_UART_H

/*
 * UART/USART 异步串口：帧格式配置、阻塞/中断/DMA 收发、空闲检测与回调。
 *
 * 句柄由调用者静态分配，base 必须是首个成员。
 * 所有接口在 handle 为 NULL 时返回 IOT_INVALID_PARAM（查询类返回 0/NULL 语义值）。
 * 同一时刻只允许一个方向上的非阻塞传输；重复启动返回 IOT_BUSY。
 */

#include "iot_def.h"
#include "iot_dma.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 数据字长 */
typedef enum {
    IOT_UART_WORD_7BIT = 7,    /* 7 位数据 */
    IOT_UART_WORD_8BIT = 8,    /* 8 位数据 */
    IOT_UART_WORD_9BIT = 9     /* 9 位数据（含校验时常见） */
} iot_uart_word_t;

/* 校验方式 */
typedef enum {
    IOT_UART_PARITY_NONE = 0,  /* 无校验 */
    IOT_UART_PARITY_EVEN,      /* 偶校验 */
    IOT_UART_PARITY_ODD        /* 奇校验 */
} iot_uart_parity_t;

/* 停止位 */
typedef enum {
    IOT_UART_STOP_1 = 0,       /* 1 个停止位 */
    IOT_UART_STOP_2            /* 2 个停止位 */
} iot_uart_stop_t;

/* 硬件流控 */
typedef enum {
    IOT_UART_FLOW_NONE = 0,    /* 无流控 */
    IOT_UART_FLOW_RTS,         /* 仅 RTS */
    IOT_UART_FLOW_CTS,         /* 仅 CTS */
    IOT_UART_FLOW_RTS_CTS      /* RTS + CTS */
} iot_uart_flow_t;

/* 收发方向，可组合 */
typedef enum {
    IOT_UART_MODE_TX = 1,      /* 仅发送 */
    IOT_UART_MODE_RX = 2,      /* 仅接收 */
    IOT_UART_MODE_TX_RX = 3    /* 收发 */
} iot_uart_mode_t;

/* UART 配置 */
typedef struct {
    uint32_t baudrate;              /* 波特率（bps），需大于 0 */

    iot_uart_word_t word_length;   /* 数据字长 */
    iot_uart_parity_t parity;      /* 校验方式 */
    iot_uart_stop_t stop_bits;     /* 停止位 */

    iot_uart_flow_t flow_control;  /* 硬件流控 */
    iot_uart_mode_t mode;          /* 收发方向 */

    bool oversampling;              /* true = 8 倍过采样，false = 16 倍 */
} iot_uart_config_t;

/* UART 事件类型，用于回调上报 */
typedef enum {
    IOT_UART_EVENT_TX_COMPLETE = 0,    /* 发送完成 */
    IOT_UART_EVENT_RX_COMPLETE,        /* 接收完成 */
    IOT_UART_EVENT_RX_HALF,            /* 接收过半（双缓冲/DMA 半传输） */
    IOT_UART_EVENT_RX_IDLE,            /* 总线空闲，接收结束 */
    IOT_UART_EVENT_ERROR,              /* 帧错误/溢出/噪声等 */
    IOT_UART_EVENT_ABORT               /* 传输被中止 */
} iot_uart_event_t;

/*
 * UART 事件回调。
 * handle    : 触发事件的 UART 句柄；
 * event     : 事件类型；
 * length    : 与该事件相关的数据长度（字节），不同事件含义见上；
 * user_data : 注册时绑定的上下文，可为 NULL。
 * 注意：执行于中断上下文，禁止阻塞。
 */
typedef void (*iot_uart_callback_t)(void *handle, iot_uart_event_t event,
                                     size_t length, void *user_data);

/* UART 句柄 */
typedef struct iot_uart {
    iot_handle_base_t base;        /* 通用句柄基类，勿移动位置 */

    iot_uart_config_t config;      /* 初始化时保存的配置副本 */

    iot_dma_t *tx_dma;      /* 发送 DMA 通道，可为 NULL（DMA 模式下由平台绑定） */
    iot_dma_t *rx_dma;      /* 接收 DMA 通道，可为 NULL */

    iot_uart_callback_t callback;  /* 事件回调，可为 NULL */
    void *user_data;                /* 回调上下文，可为 NULL */
} iot_uart_t;

/*
 * 初始化 UART。
 * handle : 句柄，不可为 NULL。
 * config : 配置，不可为 NULL，baudrate 必须大于 0。
 * 返回   : IOT_OK；IOT_INVALID_PARAM；波特率不可达返回 IOT_NOT_SUPPORTED。
 * 注意   : 成功后 base.state 为 IOT_STATE_READY。
 */
iot_status_t iot_uart_init(iot_uart_t *handle, const iot_uart_config_t *config);

/*
 * 反初始化 UART，终止所有进行中的传输。
 * 返回 : IOT_OK；IOT_INVALID_PARAM；未初始化返回 IOT_NOT_READY。
 */
iot_status_t iot_uart_deinit(iot_uart_t *handle);

/* 阻塞收发 */
/*
 * 阻塞发送。
 * data    : 待发送缓冲区，不可为 NULL。
 * length  : 字节数；为 0 时直接返回 IOT_OK。
 * timeout : 最大等待毫秒数，IOT_TIMEOUT_FOREVER 表示无限等待。
 * 返回    : IOT_OK；IOT_INVALID_PARAM；IOT_TIMEOUT；IOT_BUSY。
 */
iot_status_t iot_uart_transmit(iot_uart_t *handle, const uint8_t *data,
                                 size_t length, uint32_t timeout);

/*
 * 阻塞接收指定长度。
 * data    : 接收缓冲区，不可为 NULL。
 * length  : 期望字节数；为 0 时直接返回 IOT_OK。
 * timeout : 最大等待毫秒数。
 * 返回    : IOT_OK；IOT_INVALID_PARAM；IOT_TIMEOUT；IOT_OVERFLOW（缓冲不足）。
 */
iot_status_t iot_uart_receive(iot_uart_t *handle, uint8_t *data,
                                size_t length, uint32_t timeout);

/* 中断收发 */

/* 中断方式发送；data 与 length 所指缓冲区在完成回调前必须保持有效 */
iot_status_t iot_uart_transmit_it(iot_uart_t *handle, const uint8_t *data, size_t length);

/* 中断方式接收；data 缓冲区需保持有效直到接收完成或中止 */
iot_status_t iot_uart_receive_it(iot_uart_t *handle, uint8_t *data, size_t length);

/* DMA 收发 */

/* DMA 方式发送；缓冲区需在回调前保持有效，且注意缓存一致性 */
iot_status_t iot_uart_transmit_dma(iot_uart_t *handle, const uint8_t *data, size_t length);

/* DMA 方式接收；缓冲区需在回调前保持有效 */
iot_status_t iot_uart_receive_dma(iot_uart_t *handle, uint8_t *data, size_t length);

/* 接收至空闲 */

/*
 * 不定长接收：收到连续数据直至总线空闲，mode 选择 IT 或 DMA 实现。
 * data    : 接收缓冲区，不可为 NULL。
 * length  : 缓冲区容量，达到容量即视为完成。
 * mode    : 仅支持 IOT_MODE_IT / IOT_MODE_DMA；BLOCKING 返回 IOT_NOT_SUPPORTED。
 * 返回    : IOT_OK；IOT_INVALID_PARAM；IOT_NOT_SUPPORTED；IOT_BUSY。
 * 注意    : 实际接收长度通过 iot_uart_get_rx_count() 或 RX_IDLE 事件获取。
 */
iot_status_t iot_uart_receive_to_idle(iot_uart_t *handle, uint8_t *data,
                                        size_t length, iot_transfer_mode_t mode);

/*
 * 获取最近一次（或当前）接收的数据长度。
 * handle : 句柄，可为 NULL（返回 0）。
 * 返回   : 字节数；无接收或参数非法时为 0。
 */
size_t iot_uart_get_rx_count(iot_uart_t *handle);

/* 中止 */

/* 中止发送；未在发送时返回 IOT_OK */
iot_status_t iot_uart_abort_tx(iot_uart_t *handle);

/* 中止接收；未在接收时返回 IOT_OK */
iot_status_t iot_uart_abort_rx(iot_uart_t *handle);

/* 同时中止收发 */
iot_status_t iot_uart_abort(iot_uart_t *handle);

/* 状态 */

/* 查询当前状态；handle 为 NULL 时返回 IOT_STATE_RESET */
iot_state_t iot_uart_get_state(iot_uart_t *handle);

/* 查询最近一次错误码；handle 为 NULL 时返回 IOT_INVALID_PARAM，无错误返回 IOT_OK */
iot_status_t iot_uart_get_error(iot_uart_t *handle);

/* 回调 */

/*
 * 注册事件回调。
 * callback  : 新的回调，可为 NULL（表示注销回调）；
 * user_data : 回调上下文，可为 NULL。
 * 返回      : IOT_OK；IOT_INVALID_PARAM。
 * 注意      : 可在运行期调用，注册后覆盖旧回调。
 */
iot_status_t iot_uart_register_callback(iot_uart_t *handle,
                                          iot_uart_callback_t callback, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* IOT_UART_H */
