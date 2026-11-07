#ifndef PORT_ADC_H
#define PORT_ADC_H

/*
 * ADC 模数转换：分辨率/对齐/触发方式配置、通道规则、单次读取、
 * 中断与 DMA 连续采样、校准及满量程查询。
 *
 * 句柄由调用者静态分配，base 必须是首个成员。
 * 所有接口在 handle 为 NULL 时返回 PORT_INVALID_PARAM（查询类返回 0）。
 * DMA 模式下 buffer 需保持有效直至采集完成回调。
 */

#include "port_def.h"
#include "port_dma.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 转换分辨率 */
typedef enum {
    PORT_ADC_RESOLUTION_6BIT = 6,   /* 6 位 */
    PORT_ADC_RESOLUTION_8BIT = 8,   /* 8 位 */
    PORT_ADC_RESOLUTION_10BIT = 10, /* 10 位 */
    PORT_ADC_RESOLUTION_12BIT = 12, /* 12 位 */
    PORT_ADC_RESOLUTION_14BIT = 14, /* 14 位 */
    PORT_ADC_RESOLUTION_16BIT = 16  /* 16 位 */
} port_adc_resolution_t;

/* 数据对齐方式 */
typedef enum {
    PORT_ADC_ALIGN_RIGHT = 0,   /* 右对齐 */
    PORT_ADC_ALIGN_LEFT         /* 左对齐 */
} port_adc_align_t;

/* 转换触发源 */
typedef enum {
    PORT_ADC_TRIGGER_SOFTWARE = 0,  /* 软件触发 */
    PORT_ADC_TRIGGER_TIMER,         /* 定时器触发 */
    PORT_ADC_TRIGGER_EXTERNAL       /* 外部信号触发 */
} port_adc_trigger_t;

/* ADC 控制器配置 */
typedef struct {
    port_adc_resolution_t resolution;   /* 分辨率 */
    port_adc_align_t alignment;         /* 数据对齐 */
    port_adc_trigger_t trigger;         /* 触发源 */

    uint8_t channel_count;              /* 参与转换的通道数（规则组） */
} port_adc_config_t;

/* 单通道配置 */
typedef struct {
    uint8_t channel;        /* 硬件通道号 */
    uint8_t rank;           /* 转换顺序，从 1 开始 */
    uint32_t sample_time;   /* 采样时间，单位由平台定义（通常为 ADC 时钟周期数） */
} port_adc_channel_config_t;

/* ADC 句柄 */
typedef struct port_adc_handle {
    port_handle_base_t base;        /* 通用句柄基类，勿移动位置 */

    port_adc_config_t config;       /* 初始化时保存的配置副本 */
    port_dma_handle_t *dma;         /* 关联 DMA 通道，可为 NULL */

    port_callback_t callback;       /* 转换完成/错误回调，可为 NULL */
    void *user_data;                /* 回调上下文，可为 NULL */
} port_adc_handle_t;

/*
 * 初始化 ADC。
 * handle : 句柄，不可为 NULL。
 * config : 配置，不可为 NULL；channel_count 不应超过硬件规则组容量。
 * 返回   : PORT_OK；PORT_INVALID_PARAM；分辨率不支持返回 PORT_NOT_SUPPORTED。
 */
port_status_t port_adc_init(port_adc_handle_t *handle, const port_adc_config_t *config);

/* 反初始化 ADC；返回 PORT_OK / PORT_INVALID_PARAM / PORT_NOT_READY */
port_status_t port_adc_deinit(port_adc_handle_t *handle);

/*
 * 配置单个转换通道。
 * config : 通道配置，不可为 NULL；rank 需在 1..channel_count 范围内。
 * 返回   : PORT_OK；PORT_INVALID_PARAM；通道不存在返回 PORT_NOT_FOUND。
 * 注意   : 需在 port_adc_start() 之前调用。
 */
port_status_t port_adc_config_channel(port_adc_handle_t *handle,
                                      const port_adc_channel_config_t *config);

/* 启动连续转换（软件触发模式下开始采样）；返回 PORT_OK / PORT_INVALID_PARAM / PORT_NOT_READY */
port_status_t port_adc_start(port_adc_handle_t *handle);

/* 停止转换；返回 PORT_OK / PORT_INVALID_PARAM / PORT_NOT_READY */
port_status_t port_adc_stop(port_adc_handle_t *handle);

/*
 * 单次读取指定通道的转换结果。
 * channel : 通道号；
 * value   : 输出结果，不可为 NULL；
 * timeout : 最大等待毫秒数。
 * 返回    : PORT_OK；PORT_INVALID_PARAM；PORT_TIMEOUT；通道非法 PORT_NOT_FOUND。
 * 注意    : 内部会临时切换通道，不宜在高频循环中频繁调用。
 */
port_status_t port_adc_read(port_adc_handle_t *handle, uint8_t channel,
                            uint32_t *value, uint32_t timeout);

/* 中断方式启动转换，完成后通过回调上报 */
port_status_t port_adc_start_it(port_adc_handle_t *handle);

/*
 * DMA 方式连续转换，结果写入 buffer。
 * buffer : 结果缓冲区，不可为 NULL；
 * length : 采样点个数（按元素计，非字节）。
 * 返回   : PORT_OK；PORT_INVALID_PARAM；DMA 不可用返回 PORT_NOT_SUPPORTED。
 */
port_status_t port_adc_start_dma(port_adc_handle_t *handle, uint32_t *buffer, size_t length);

/*
 * 执行 ADC 自校准，提高精度。
 * 返回 : PORT_OK；PORT_INVALID_PARAM；平台无校准能力返回 PORT_NOT_SUPPORTED。
 * 注意 : 应在初始化后、启动转换前调用。
 */
port_status_t port_adc_calibrate(port_adc_handle_t *handle);

/*
 * 获取当前分辨率对应的满量程值（如 12 位为 4095）。
 * handle : 句柄，可为 NULL。
 * 返回   : 满量程值；handle 为 NULL 或未初始化时返回 0。
 */
uint32_t port_adc_get_max_value(port_adc_handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* PORT_ADC_H */
