#ifndef HAL_DMA_H
#define HAL_DMA_H

/*
 * DMA 控制器抽象：通道配置、启动/停止、挂起/恢复与剩余计数查询。
 *
 * 句柄由调用者静态分配，首个成员为 hal_handle_base_t。
 * 所有接口在 handle 为 NULL 时返回 HAL_INVALID_PARAM（查询类返回 0）。
 * 传输完成、半传输、错误等事件通过 callback 上报，回调运行于中断上下文。
 */

#include "hal_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 传输方向 */
typedef enum {
    HAL_DMA_MEMORY_TO_MEMORY = 0,  /* 存储器到存储器 */
    HAL_DMA_MEMORY_TO_PERIPH,      /* 存储器到外设 */
    HAL_DMA_PERIPH_TO_MEMORY       /* 外设到存储器 */
} hal_dma_direction_e;

/* 数据宽度 */
typedef enum {
    HAL_DMA_WIDTH_8BIT = 0,    /* 8 位 */
    HAL_DMA_WIDTH_16BIT,       /* 16 位 */
    HAL_DMA_WIDTH_32BIT        /* 32 位 */
} hal_dma_width_e;

/* 通道优先级 */
typedef enum {
    HAL_DMA_PRIORITY_LOW = 0,  /* 低 */
    HAL_DMA_PRIORITY_MEDIUM,   /* 中 */
    HAL_DMA_PRIORITY_HIGH,     /* 高 */
    HAL_DMA_PRIORITY_VERY_HIGH /* 最高 */
} hal_dma_priority_e;

/* 传输模式 */
typedef enum {
    HAL_DMA_MODE_NORMAL = 0,   /* 单次模式，传输完成后停止 */
    HAL_DMA_MODE_CIRCULAR      /* 循环模式，长度回绕后继续 */
} hal_dma_mode_e;

/* DMA 通道配置 */
typedef struct {
    hal_dma_direction_e direction; /* 传输方向 */

    hal_dma_width_e src_width;     /* 源数据宽度 */
    hal_dma_width_e dst_width;     /* 目的数据宽度，与源不同时硬件可能不支持 */

    hal_dma_priority_e priority;   /* 通道优先级 */
    hal_dma_mode_e mode;           /* 传输模式 */

    bool src_increment;             /* 源地址是否自增；外设地址通常为 false */
    bool dst_increment;             /* 目的地址是否自增；外设地址通常为 false */

    uint32_t request;               /* 硬件请求源编号，存储器到存储器时忽略 */
} hal_dma_config_t;

/* DMA 句柄 */
typedef struct hal_dma {
    hal_handle_base_t base;        /* 通用句柄基类，勿移动位置 */

    hal_dma_config_t config;       /* 初始化时保存的配置副本 */

    hal_callback_t callback;       /* 事件回调，可为 NULL */
    void *user_data;                /* 回调上下文，可为 NULL */
} hal_dma_t;

/*
 * 初始化 DMA 通道。
 * handle : 句柄，不可为 NULL。
 * config : 配置，不可为 NULL。
 * 返回   : HAL_OK；HAL_INVALID_PARAM；参数组合不受支持时 HAL_NOT_SUPPORTED。
 * 注意   : 成功后 base.state 进入 HAL_STATE_READY。
 */
int hal_dma_init(hal_dma_t *handle, const hal_dma_config_t *config);

/*
 * 反初始化 DMA 通道。
 * handle : 句柄，不可为 NULL。
 * 返回   : HAL_OK；HAL_INVALID_PARAM；未初始化返回 HAL_NOT_READY。
 */
int hal_dma_deinit(hal_dma_t *handle);

/*
 * 启动一次 DMA 传输。
 * handle : 句柄，不可为 NULL。
 * src    : 源地址（内存或外设寄存器地址）。
 * dst    : 目的地址。
 * length : 传输数据个数（以宽度为单位，非字节数）。
 * 返回   : HAL_OK；HAL_INVALID_PARAM；通道忙返回 HAL_BUSY。
 * 注意   : length 为 0 时不做任何传输并返回 HAL_OK。
 */
int hal_dma_start(hal_dma_t *handle, uintptr_t src, uintptr_t dst, size_t length);

/*
 * 停止当前传输。
 * handle : 句柄，不可为 NULL。
 * 返回   : HAL_OK；HAL_INVALID_PARAM。
 * 注意   : 停止后不会触发完成回调。
 */
int hal_dma_stop(hal_dma_t *handle);

/*
 * 挂起传输，保留剩余计数与现场。
 * 返回 : HAL_OK；HAL_INVALID_PARAM；无活动传输返回 HAL_NOT_READY。
 */
int hal_dma_suspend(hal_dma_t *handle);

/*
 * 恢复被挂起的传输。
 * 返回 : HAL_OK；HAL_INVALID_PARAM；非挂起状态返回 HAL_NOT_READY。
 */
int hal_dma_resume(hal_dma_t *handle);

/*
 * 查询剩余未传输的数据个数。
 * handle : 句柄，可为 NULL（此时返回 0）。
 * 返回   : 剩余个数；无活动传输时返回 0。
 */
size_t hal_dma_get_remaining(hal_dma_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* HAL_DMA_H */
