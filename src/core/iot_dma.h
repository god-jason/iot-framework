#ifndef IOT_DMA_H
#define IOT_DMA_H

/*
 * DMA 控制器抽象：通道配置、启动/停止、挂起/恢复与剩余计数查询。
 *
 * 句柄由调用者静态分配，首个成员为 iot_handle_base_t。
 * 所有接口在 handle 为 NULL 时返回 IOT_INVALID_PARAM（查询类返回 0）。
 * 传输完成、半传输、错误等事件通过 callback 上报，回调运行于中断上下文。
 */

#include "iot_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 传输方向 */
typedef enum {
    IOT_DMA_MEMORY_TO_MEMORY = 0,  /* 存储器到存储器 */
    IOT_DMA_MEMORY_TO_PERIPH,      /* 存储器到外设 */
    IOT_DMA_PERIPH_TO_MEMORY       /* 外设到存储器 */
} iot_dma_direction_t;

/* 数据宽度 */
typedef enum {
    IOT_DMA_WIDTH_8BIT = 0,    /* 8 位 */
    IOT_DMA_WIDTH_16BIT,       /* 16 位 */
    IOT_DMA_WIDTH_32BIT        /* 32 位 */
} iot_dma_width_t;

/* 通道优先级 */
typedef enum {
    IOT_DMA_PRIORITY_LOW = 0,  /* 低 */
    IOT_DMA_PRIORITY_MEDIUM,   /* 中 */
    IOT_DMA_PRIORITY_HIGH,     /* 高 */
    IOT_DMA_PRIORITY_VERY_HIGH /* 最高 */
} iot_dma_priority_t;

/* 传输模式 */
typedef enum {
    IOT_DMA_MODE_NORMAL = 0,   /* 单次模式，传输完成后停止 */
    IOT_DMA_MODE_CIRCULAR      /* 循环模式，长度回绕后继续 */
} iot_dma_mode_t;

/* DMA 通道配置 */
typedef struct {
    iot_dma_direction_t direction; /* 传输方向 */

    iot_dma_width_t src_width;     /* 源数据宽度 */
    iot_dma_width_t dst_width;     /* 目的数据宽度，与源不同时硬件可能不支持 */

    iot_dma_priority_t priority;   /* 通道优先级 */
    iot_dma_mode_t mode;           /* 传输模式 */

    bool src_increment;             /* 源地址是否自增；外设地址通常为 false */
    bool dst_increment;             /* 目的地址是否自增；外设地址通常为 false */

    uint32_t request;               /* 硬件请求源编号，存储器到存储器时忽略 */
} iot_dma_config_t;

/* DMA 句柄 */
typedef struct iot_dma {
    iot_handle_base_t base;        /* 通用句柄基类，勿移动位置 */

    iot_dma_config_t config;       /* 初始化时保存的配置副本 */

    iot_callback_t callback;       /* 事件回调，可为 NULL */
    void *user_data;                /* 回调上下文，可为 NULL */
} iot_dma_t;

/*
 * 初始化 DMA 通道。
 * handle : 句柄，不可为 NULL。
 * config : 配置，不可为 NULL。
 * 返回   : IOT_OK；IOT_INVALID_PARAM；参数组合不受支持时 IOT_NOT_SUPPORTED。
 * 注意   : 成功后 base.state 进入 IOT_STATE_READY。
 */
iot_status_t iot_dma_init(iot_dma_t *handle, const iot_dma_config_t *config);

/*
 * 反初始化 DMA 通道。
 * handle : 句柄，不可为 NULL。
 * 返回   : IOT_OK；IOT_INVALID_PARAM；未初始化返回 IOT_NOT_READY。
 */
iot_status_t iot_dma_deinit(iot_dma_t *handle);

/*
 * 启动一次 DMA 传输。
 * handle : 句柄，不可为 NULL。
 * src    : 源地址（内存或外设寄存器地址）。
 * dst    : 目的地址。
 * length : 传输数据个数（以宽度为单位，非字节数）。
 * 返回   : IOT_OK；IOT_INVALID_PARAM；通道忙返回 IOT_BUSY。
 * 注意   : length 为 0 时不做任何传输并返回 IOT_OK。
 */
iot_status_t iot_dma_start(iot_dma_t *handle, uintptr_t src, uintptr_t dst, size_t length);

/*
 * 停止当前传输。
 * handle : 句柄，不可为 NULL。
 * 返回   : IOT_OK；IOT_INVALID_PARAM。
 * 注意   : 停止后不会触发完成回调。
 */
iot_status_t iot_dma_stop(iot_dma_t *handle);

/*
 * 挂起传输，保留剩余计数与现场。
 * 返回 : IOT_OK；IOT_INVALID_PARAM；无活动传输返回 IOT_NOT_READY。
 */
iot_status_t iot_dma_suspend(iot_dma_t *handle);

/*
 * 恢复被挂起的传输。
 * 返回 : IOT_OK；IOT_INVALID_PARAM；非挂起状态返回 IOT_NOT_READY。
 */
iot_status_t iot_dma_resume(iot_dma_t *handle);

/*
 * 查询剩余未传输的数据个数。
 * handle : 句柄，可为 NULL（此时返回 0）。
 * 返回   : 剩余个数；无活动传输时返回 0。
 */
size_t iot_dma_get_remaining(iot_dma_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* IOT_DMA_H */
