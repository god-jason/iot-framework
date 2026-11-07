#ifndef PORT_I2S_H
#define PORT_I2S_H

/*
 * I2S 音频总线：主从模式、协议标准、采样率与数据位宽配置，阻塞/DMA 传输。
 *
 * 句柄由调用者静态分配，base 必须是首个成员。
 * 所有接口在 handle 为 NULL 时返回 PORT_INVALID_PARAM（查询类返回默认值）。
 * 传输长度以字节计；DMA 缓冲区需保持有效直至完成回调。
 */

#include "port_def.h"
#include "port_dma.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 工作模式 */
typedef enum {
    PORT_I2S_MODE_MASTER_TX = 0,/* 主机发送 */
    PORT_I2S_MODE_MASTER_RX,    /* 主机接收 */
    PORT_I2S_MODE_SLAVE_TX,     /* 从机发送 */
    PORT_I2S_MODE_SLAVE_RX      /* 从机接收 */
} port_i2s_mode_t;

/* 协议标准 */
typedef enum {
    PORT_I2S_STANDARD_PHILIPS = 0,  /* Philips 标准 I2S */
    PORT_I2S_STANDARD_MSB,          /* 左对齐（MSB） */
    PORT_I2S_STANDARD_LSB,          /* 右对齐（LSB） */
    PORT_I2S_STANDARD_PCM           /* PCM/DSP 模式 */
} port_i2s_standard_t;

/* 采样数据位宽 */
typedef enum {
    PORT_I2S_DATA_16BIT = 0,    /* 16 位 */
    PORT_I2S_DATA_24BIT,        /* 24 位 */
    PORT_I2S_DATA_32BIT         /* 32 位 */
} port_i2s_data_size_t;

/* I2S 配置 */
typedef struct {
    port_i2s_mode_t mode;           /* 工作模式 */
    port_i2s_standard_t standard;   /* 协议标准 */

    uint32_t sample_rate;           /* 采样率（Hz），如 44100/48000 */
    port_i2s_data_size_t data_size; /* 采样数据位宽 */

    bool master_clock;              /* 是否输出主时钟 MCLK */
} port_i2s_config_t;

/* I2S 句柄 */
typedef struct port_i2s_handle {
    port_handle_base_t base;        /* 通用句柄基类，勿移动位置 */
    port_i2s_config_t config;       /* 初始化时保存的配置副本 */

    port_dma_handle_t *tx_dma;      /* 发送 DMA 通道，可为 NULL */
    port_dma_handle_t *rx_dma;      /* 接收 DMA 通道，可为 NULL */

    port_callback_t callback;       /* 事件回调，可为 NULL */
    void *user_data;                /* 回调上下文，可为 NULL */
} port_i2s_handle_t;

/*
 * 初始化 I2S。
 * handle : 句柄，不可为 NULL。
 * config : 配置，不可为 NULL，sample_rate 必须大于 0。
 * 返回   : PORT_OK；PORT_INVALID_PARAM；采样率不可达返回 PORT_NOT_SUPPORTED。
 */
port_status_t port_i2s_init(port_i2s_handle_t *handle, const port_i2s_config_t *config);

/* 反初始化 I2S；返回 PORT_OK / PORT_INVALID_PARAM / PORT_NOT_READY */
port_status_t port_i2s_deinit(port_i2s_handle_t *handle);

/*
 * 阻塞发送。
 * data    : 发送缓冲区，length 为 0 时可为 NULL。
 * length  : 字节数。
 * timeout : 最大等待毫秒数。
 * 返回    : PORT_OK；PORT_INVALID_PARAM；PORT_TIMEOUT；PORT_BUSY。
 */
port_status_t port_i2s_transmit(port_i2s_handle_t *handle, const void *data,
                                size_t length, uint32_t timeout);

/* 阻塞接收；data 缓冲区不可为 NULL */
port_status_t port_i2s_receive(port_i2s_handle_t *handle, void *data,
                               size_t length, uint32_t timeout);

/* DMA 发送；data 需在完成回调前保持有效 */
port_status_t port_i2s_transmit_dma(port_i2s_handle_t *handle, const void *data, size_t length);

/* DMA 接收；data 需在完成回调前保持有效 */
port_status_t port_i2s_receive_dma(port_i2s_handle_t *handle, void *data, size_t length);

/* 中止当前传输；无传输时返回 PORT_OK */
port_status_t port_i2s_abort(port_i2s_handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* PORT_I2S_H */
