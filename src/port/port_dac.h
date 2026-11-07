#ifndef PORT_DAC_H
#define PORT_DAC_H

#include "port_def.h"
#include "port_dma.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PORT_DAC_ALIGN_8BIT = 0,
    PORT_DAC_ALIGN_12BIT_RIGHT,
    PORT_DAC_ALIGN_12BIT_LEFT
} port_dac_alignment_t;

typedef struct {
    uint8_t channel;
    port_dac_alignment_t alignment;
} port_dac_config_t;

typedef struct port_dac_handle {
    port_handle_base_t base;
    port_dac_config_t config;
    port_dma_handle_t *dma;
} port_dac_handle_t;

port_status_t port_dac_init(port_dac_handle_t *handle, const port_dac_config_t *config);
port_status_t port_dac_deinit(port_dac_handle_t *handle);

port_status_t port_dac_start(port_dac_handle_t *handle);
port_status_t port_dac_stop(port_dac_handle_t *handle);

port_status_t port_dac_write(port_dac_handle_t *handle, uint32_t value);
port_status_t port_dac_write_dma(port_dac_handle_t *handle, const uint32_t *buffer, size_t length);

#ifdef __cplusplus
}
#endif

#endif /* PORT_DAC_H */
