#ifndef IOT_SDIO_H
#define IOT_SDIO_H

/*
 * SDIO / SD 主机接口：总线位宽与时钟配置、按块读写（轮询或 DMA）。
 *
 * 句柄由调用者静态分配，base 必须是首个成员。
 * 所有接口在 handle 为 NULL 时返回 IOT_INVALID_PARAM。
 * block 为物理块号，块大小由平台固定（通常 512 字节）；
 * buffer 容量应不小于 block_count * 块大小。
 */

#include "iot_def.h"
#include "iot_dma.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 总线位宽 */
typedef enum {
    IOT_SDIO_BUS_1BIT = 0,     /* 1 线 */
    IOT_SDIO_BUS_4BIT,         /* 4 线 */
    IOT_SDIO_BUS_8BIT          /* 8 线（eMMC 等） */
} iot_sdio_bus_width_t;

/* SDIO 配置 */
typedef struct {
    uint32_t clock_frequency;       /* 总线时钟频率（Hz） */
    iot_sdio_bus_width_t bus_width;/* 总线位宽 */
} iot_sdio_config_t;

/* SDIO 句柄 */
typedef struct iot_sdio {
    iot_handle_base_t base;    /* 通用句柄基类，勿移动位置 */
    iot_sdio_config_t config;  /* 初始化时保存的配置副本 */
    iot_dma_t *dma;     /* 关联 DMA 通道，可为 NULL */
    iot_callback_t callback;   /* 传输完成/错误回调，可为 NULL */
    void *user_data;            /* 回调上下文，可为 NULL */
} iot_sdio_t;

/*
 * 初始化 SDIO。
 * handle : 句柄，不可为 NULL。
 * config : 配置，不可为 NULL，clock_frequency 必须大于 0。
 * 返回   : IOT_OK；IOT_INVALID_PARAM；位宽不支持返回 IOT_NOT_SUPPORTED。
 */
iot_status_t iot_sdio_init(iot_sdio_t *handle, const iot_sdio_config_t *config);

/* 反初始化 SDIO；返回 IOT_OK / IOT_INVALID_PARAM / IOT_NOT_READY */
iot_status_t iot_sdio_deinit(iot_sdio_t *handle);

/*
 * 阻塞读多个块。
 * block       : 起始块号；
 * buffer      : 接收缓冲区，不可为 NULL；
 * block_count : 块数，0 时直接返回 IOT_OK；
 * timeout     : 最大等待毫秒数。
 * 返回        : IOT_OK；IOT_INVALID_PARAM；IOT_TIMEOUT；IOT_CRC_ERROR。
 */
iot_status_t iot_sdio_read(iot_sdio_t *handle, uint32_t block, void *buffer,
                             size_t block_count, uint32_t timeout);

/* 阻塞写多个块；buffer 为源数据，其余语义同 read */
iot_status_t iot_sdio_write(iot_sdio_t *handle, uint32_t block,
                              const void *buffer, size_t block_count, uint32_t timeout);

/* DMA 读多个块；buffer 需在完成回调前保持有效 */
iot_status_t iot_sdio_read_dma(iot_sdio_t *handle, uint32_t block,
                                 void *buffer, size_t block_count);

/* DMA 写多个块；buffer 需在完成回调前保持有效 */
iot_status_t iot_sdio_write_dma(iot_sdio_t *handle, uint32_t block,
                                  const void *buffer, size_t block_count);

#ifdef __cplusplus
}
#endif

#endif /* IOT_SDIO_H */
