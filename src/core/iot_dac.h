#ifndef IOT_DAC_H
#define IOT_DAC_H

/*
 * DAC 数模转换：通道与数据对齐配置、启停、单点输出及 DMA 波形输出。
 *
 * 句柄由调用者静态分配，base 必须是首个成员。
 * 所有接口在 handle 为 NULL 时返回 IOT_INVALID_PARAM。
 * DMA 输出时 buffer 需保持有效直至传输完成回调。
 */

#include "iot_def.h"
#include "iot_dma.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 数据对齐方式 */
typedef enum {
    IOT_DAC_ALIGN_8BIT = 0,        /* 8 位右对齐 */
    IOT_DAC_ALIGN_12BIT_RIGHT,     /* 12 位右对齐 */
    IOT_DAC_ALIGN_12BIT_LEFT       /* 12 位左对齐 */
} iot_dac_alignment_t;

/* DAC 配置 */
typedef struct {
    uint8_t channel;                /* 输出通道号 */
    iot_dac_alignment_t alignment; /* 数据对齐方式 */
} iot_dac_config_t;

/* DAC 句柄 */
typedef struct iot_dac {
    iot_handle_base_t base;        /* 通用句柄基类，勿移动位置 */
    iot_dac_config_t config;       /* 初始化时保存的配置副本 */
    iot_dma_t *dma;         /* 关联 DMA 通道，可为 NULL */
} iot_dac_t;

/*
 * 初始化 DAC。
 * handle : 句柄，不可为 NULL。
 * config : 配置，不可为 NULL；通道号需在硬件范围内。
 * 返回   : IOT_OK；IOT_INVALID_PARAM；通道不存在返回 IOT_NOT_FOUND。
 */
iot_status_t iot_dac_init(iot_dac_t *handle, const iot_dac_config_t *config);

/* 反初始化 DAC；返回 IOT_OK / IOT_INVALID_PARAM / IOT_NOT_READY */
iot_status_t iot_dac_deinit(iot_dac_t *handle);

/* 使能 DAC 输出；返回 IOT_OK / IOT_INVALID_PARAM / IOT_NOT_READY */
iot_status_t iot_dac_start(iot_dac_t *handle);

/* 关闭 DAC 输出，输出保持为高阻或上次值由平台决定 */
iot_status_t iot_dac_stop(iot_dac_t *handle);

/*
 * 输出单个采样值。
 * value : 采样值，超出当前对齐位宽的高位会被平台截断或返回 IOT_OVERFLOW。
 * 返回  : IOT_OK；IOT_INVALID_PARAM；未启动返回 IOT_NOT_READY。
 */
iot_status_t iot_dac_write(iot_dac_t *handle, uint32_t value);

/*
 * DMA 方式连续输出波形。
 * buffer : 采样缓冲区，不可为 NULL；
 * length : 采样点个数（按元素计，非字节）。
 * 返回   : IOT_OK；IOT_INVALID_PARAM；DMA 不可用返回 IOT_NOT_SUPPORTED。
 */
iot_status_t iot_dac_write_dma(iot_dac_t *handle, const uint32_t *buffer, size_t length);

#ifdef __cplusplus
}
#endif

#endif /* IOT_DAC_H */
