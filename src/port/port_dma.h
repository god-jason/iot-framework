#ifndef PORT_DMA_H
#define PORT_DMA_H

#include "port_def.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PORT_DMA_MEMORY_TO_MEMORY = 0,
    PORT_DMA_MEMORY_TO_PERIPH,
    PORT_DMA_PERIPH_TO_MEMORY
} port_dma_direction_t;

typedef enum {
    PORT_DMA_WIDTH_8BIT = 0,
    PORT_DMA_WIDTH_16BIT,
    PORT_DMA_WIDTH_32BIT
} port_dma_width_t;

typedef enum {
    PORT_DMA_PRIORITY_LOW = 0,
    PORT_DMA_PRIORITY_MEDIUM,
    PORT_DMA_PRIORITY_HIGH,
    PORT_DMA_PRIORITY_VERY_HIGH
} port_dma_priority_t;

typedef enum {
    PORT_DMA_MODE_NORMAL = 0,
    PORT_DMA_MODE_CIRCULAR
} port_dma_mode_t;

typedef struct {
    port_dma_direction_t direction;

    port_dma_width_t src_width;
    port_dma_width_t dst_width;

    port_dma_priority_t priority;
    port_dma_mode_t mode;

    bool src_increment;
    bool dst_increment;

    uint32_t request;
} port_dma_config_t;

typedef struct port_dma_handle {
    port_handle_base_t base;

    port_dma_config_t config;

    port_callback_t callback;
    void *user_data;
} port_dma_handle_t;

port_status_t port_dma_init(port_dma_handle_t *handle, const port_dma_config_t *config);
port_status_t port_dma_deinit(port_dma_handle_t *handle);

port_status_t port_dma_start(port_dma_handle_t *handle, uintptr_t src, uintptr_t dst, size_t length);
port_status_t port_dma_stop(port_dma_handle_t *handle);

port_status_t port_dma_suspend(port_dma_handle_t *handle);
port_status_t port_dma_resume(port_dma_handle_t *handle);

size_t port_dma_get_remaining(port_dma_handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* PORT_DMA_H */
