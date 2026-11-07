#ifndef PORT_SDIO_H
#define PORT_SDIO_H

#include "port_def.h"
#include "port_dma.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PORT_SDIO_BUS_1BIT = 0,
    PORT_SDIO_BUS_4BIT,
    PORT_SDIO_BUS_8BIT
} port_sdio_bus_width_t;

typedef struct {
    uint32_t clock_frequency;
    port_sdio_bus_width_t bus_width;
} port_sdio_config_t;

typedef struct port_sdio_handle {
    port_handle_base_t base;
    port_sdio_config_t config;
    port_dma_handle_t *dma;
    port_callback_t callback;
    void *user_data;
} port_sdio_handle_t;

port_status_t port_sdio_init(port_sdio_handle_t *handle, const port_sdio_config_t *config);
port_status_t port_sdio_deinit(port_sdio_handle_t *handle);

port_status_t port_sdio_read(port_sdio_handle_t *handle, uint32_t block, void *buffer,
                             size_t block_count, uint32_t timeout);
port_status_t port_sdio_write(port_sdio_handle_t *handle, uint32_t block,
                              const void *buffer, size_t block_count, uint32_t timeout);

port_status_t port_sdio_read_dma(port_sdio_handle_t *handle, uint32_t block,
                                 void *buffer, size_t block_count);
port_status_t port_sdio_write_dma(port_sdio_handle_t *handle, uint32_t block,
                                  const void *buffer, size_t block_count);

#ifdef __cplusplus
}
#endif

#endif /* PORT_SDIO_H */
