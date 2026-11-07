#ifndef IOT_SPI_H
#define IOT_SPI_H

/*
 * SPI 主机/从机：模式、方向、时钟极性/相位与位序配置，阻塞及 DMA 传输。
 *
 * 句柄由调用者静态分配，base 必须是首个成员。
 * 所有接口在 handle 为 NULL 时返回 IOT_INVALID_PARAM（查询类返回默认值）。
 * DMA 传输期间，tx/rx 缓冲区必须保持有效直至完成回调。
 */

#include "iot_def.h"
#include "iot_dma.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 主从模式 */
typedef enum {
    IOT_SPI_MODE_MASTER = 0,   /* 主机，输出时钟 */
    IOT_SPI_MODE_SLAVE         /* 从机，由外部提供时钟 */
} iot_spi_mode_t;

/* 数据线方向 */
typedef enum {
    IOT_SPI_DIRECTION_2LINES = 0,  /* 全双工，两根数据线 */
    IOT_SPI_DIRECTION_2LINES_RXONLY,/* 仅接收（两线） */
    IOT_SPI_DIRECTION_1LINE        /* 半双工，单数据线 */
} iot_spi_direction_t;

/* 时钟极性 */
typedef enum {
    IOT_SPI_CPOL_LOW = 0,      /* 空闲时 SCK 为低 */
    IOT_SPI_CPOL_HIGH          /* 空闲时 SCK 为高 */
} iot_spi_cpol_t;

/* 时钟相位 */
typedef enum {
    IOT_SPI_CPHA_1EDGE = 0,    /* 第一个边沿采样 */
    IOT_SPI_CPHA_2EDGE         /* 第二个边沿采样 */
} iot_spi_cpha_t;

/* 位序 */
typedef enum {
    IOT_SPI_BITORDER_MSB = 0,  /* 高位先发 */
    IOT_SPI_BITORDER_LSB       /* 低位先发 */
} iot_spi_bitorder_t;

/* SPI 配置 */
typedef struct {
    iot_spi_mode_t mode;           /* 主从模式 */
    iot_spi_direction_t direction; /* 数据线方向 */

    uint32_t baudrate;              /* 目标时钟频率（Hz），仅主机模式有效 */

    iot_spi_cpol_t cpol;           /* 时钟极性 */
    iot_spi_cpha_t cpha;           /* 时钟相位 */
    iot_spi_bitorder_t bit_order;  /* 位序 */

    uint8_t data_bits;              /* 数据位宽，通常为 8 或 16 */
} iot_spi_config_t;

/* SPI 句柄 */
typedef struct iot_spi {
    iot_handle_base_t base;        /* 通用句柄基类，勿移动位置 */

    iot_spi_config_t config;       /* 初始化时保存的配置副本 */

    iot_dma_t *tx_dma;      /* 发送 DMA 通道，可为 NULL */
    iot_dma_t *rx_dma;      /* 接收 DMA 通道，可为 NULL */

    iot_callback_t callback;       /* 事件回调，可为 NULL */
    void *user_data;                /* 回调上下文，可为 NULL */
} iot_spi_t;

/*
 * 初始化 SPI。
 * handle : 句柄，不可为 NULL。
 * config : 配置，不可为 NULL，data_bits 需为平台支持的值。
 * 返回   : IOT_OK；IOT_INVALID_PARAM；位宽/速率不支持返回 IOT_NOT_SUPPORTED。
 */
iot_status_t iot_spi_init(iot_spi_t *handle, const iot_spi_config_t *config);

/* 反初始化 SPI，终止进行中的传输；返回 IOT_OK / IOT_INVALID_PARAM / IOT_NOT_READY */
iot_status_t iot_spi_deinit(iot_spi_t *handle);

/*
 * 阻塞发送。
 * data    : 发送缓冲区，不可为 NULL（length 为 0 时忽略）。
 * length  : 传输长度，单位为数据位宽对应的字（不是字节）。
 * timeout : 最大等待毫秒数，IOT_TIMEOUT_FOREVER 表示无限等待。
 * 返回    : IOT_OK；IOT_INVALID_PARAM；IOT_TIMEOUT；IOT_BUSY。
 */
iot_status_t iot_spi_transmit(iot_spi_t *handle, const void *data,
                                size_t length, uint32_t timeout);

/* 阻塞接收；data 缓冲区不可为 NULL，其余语义同 transmit */
iot_status_t iot_spi_receive(iot_spi_t *handle, void *data,
                               size_t length, uint32_t timeout);

/*
 * 阻塞全双工传输：边发边收。
 * tx_data : 发送缓冲区，可为 NULL（此时发送固定填充值）；
 * rx_data : 接收缓冲区，可为 NULL（此时丢弃接收数据）；
 * 返回    : IOT_OK；IOT_INVALID_PARAM（两者均为 NULL 且 length 非 0）；IOT_TIMEOUT。
 */
iot_status_t iot_spi_transfer(iot_spi_t *handle, const void *tx_data,
                                void *rx_data, size_t length, uint32_t timeout);

/* DMA 发送；data 需在完成回调前保持有效 */
iot_status_t iot_spi_transmit_dma(iot_spi_t *handle, const void *data, size_t length);

/* DMA 接收；data 需在完成回调前保持有效 */
iot_status_t iot_spi_receive_dma(iot_spi_t *handle, void *data, size_t length);

/* DMA 全双工传输；tx_data/rx_data 可为 NULL，语义同 iot_spi_transfer */
iot_status_t iot_spi_transfer_dma(iot_spi_t *handle, const void *tx_data,
                                    void *rx_data, size_t length);

/*
 * 运行期修改时钟频率（仅主机模式）。
 * baudrate : 目标频率（Hz），实际值由平台分频取最接近值。
 * 返回     : IOT_OK；IOT_INVALID_PARAM；从机模式返回 IOT_NOT_SUPPORTED。
 */
iot_status_t iot_spi_set_speed(iot_spi_t *handle, uint32_t baudrate);

/* 中止当前传输；无传输时返回 IOT_OK */
iot_status_t iot_spi_abort(iot_spi_t *handle);

/* 查询当前状态；handle 为 NULL 时返回 IOT_STATE_RESET */
iot_state_t iot_spi_get_state(iot_spi_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* IOT_SPI_H */
