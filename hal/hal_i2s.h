#ifndef HAL_I2S_H
#define HAL_I2S_H

/*
 * I2S 音频总线：主从模式、协议标准、采样率与数据位宽配置，阻塞/DMA 传输。
 *
 * 句柄由调用者静态分配，base 必须是首个成员。
 * 所有接口在 handle 为 NULL 时返回 HAL_INVALID_PARAM（查询类返回默认值）。
 * 传输长度以字节计；DMA 缓冲区需保持有效直至完成回调。
 */

#include "hal_def.h"
#include "hal_dma.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 工作模式 */
typedef enum {
    HAL_I2S_MODE_MASTER_TX = 0,/* 主机发送 */
    HAL_I2S_MODE_MASTER_RX,    /* 主机接收 */
    HAL_I2S_MODE_SLAVE_TX,     /* 从机发送 */
    HAL_I2S_MODE_SLAVE_RX      /* 从机接收 */
} hal_i2s_mode_e;

/* 协议标准 */
typedef enum {
    HAL_I2S_STANDARD_PHILIPS = 0,  /* Philips 标准 I2S */
    HAL_I2S_STANDARD_MSB,          /* 左对齐（MSB） */
    HAL_I2S_STANDARD_LSB,          /* 右对齐（LSB） */
    HAL_I2S_STANDARD_PCM           /* PCM/DSP 模式 */
} hal_i2s_standard_e;

/* 采样数据位宽 */
typedef enum {
    HAL_I2S_DATA_16BIT = 0,    /* 16 位 */
    HAL_I2S_DATA_24BIT,        /* 24 位 */
    HAL_I2S_DATA_32BIT         /* 32 位 */
} hal_i2s_data_size_e;

/* I2S 配置 */
typedef struct {
    hal_i2s_mode_e mode;           /* 工作模式 */
    hal_i2s_standard_e standard;   /* 协议标准 */

    uint32_t sample_rate;           /* 采样率（Hz），如 44100/48000 */
    hal_i2s_data_size_e data_size; /* 采样数据位宽 */

    bool master_clock;              /* 是否输出主时钟 MCLK */
} hal_i2s_config_t;

/* I2S 句柄 */
typedef struct hal_i2s {
    hal_handle_base_t base;        /* 通用句柄基类，勿移动位置 */
    hal_i2s_config_t config;       /* 初始化时保存的配置副本 */

    hal_dma_t *tx_dma;      /* 发送 DMA 通道，可为 NULL */
    hal_dma_t *rx_dma;      /* 接收 DMA 通道，可为 NULL */

    hal_callback_t callback;       /* 事件回调，可为 NULL */
    void *user_data;                /* 回调上下文，可为 NULL */
} hal_i2s_t;

/*
 * 初始化 I2S。
 * handle : 句柄，不可为 NULL。
 * config : 配置，不可为 NULL，sample_rate 必须大于 0。
 * 返回   : HAL_OK；HAL_INVALID_PARAM；采样率不可达返回 HAL_NOT_SUPPORTED。
 */
int hal_i2s_init(hal_i2s_t *handle, const hal_i2s_config_t *config);

/* 反初始化 I2S；返回 HAL_OK / HAL_INVALID_PARAM / HAL_NOT_READY */
int hal_i2s_deinit(hal_i2s_t *handle);

/*
 * 阻塞发送。
 * data    : 发送缓冲区，length 为 0 时可为 NULL。
 * length  : 字节数。
 * timeout : 最大等待毫秒数。
 * 返回    : HAL_OK；HAL_INVALID_PARAM；HAL_TIMEOUT；HAL_BUSY。
 */
int hal_i2s_transmit(hal_i2s_t *handle, const void *data,
                                size_t length, uint32_t timeout);

/* 阻塞接收；data 缓冲区不可为 NULL */
int hal_i2s_receive(hal_i2s_t *handle, void *data,
                               size_t length, uint32_t timeout);

/* DMA 发送；data 需在完成回调前保持有效 */
int hal_i2s_transmit_dma(hal_i2s_t *handle, const void *data, size_t length);

/* DMA 接收；data 需在完成回调前保持有效 */
int hal_i2s_receive_dma(hal_i2s_t *handle, void *data, size_t length);

/* 中止当前传输；无传输时返回 HAL_OK */
int hal_i2s_abort(hal_i2s_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* HAL_I2S_H */
