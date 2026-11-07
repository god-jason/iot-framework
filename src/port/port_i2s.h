#ifndef PORT_I2S_H
#define PORT_I2S_H

#include "port_def.h"
#include "port_dma.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PORT_I2S_MODE_MASTER_TX = 0,
    PORT_I2S_MODE_MASTER_RX,
    PORT_I2S_MODE_SLAVE_TX,
    PORT_I2S_MODE_SLAVE_RX
} port_i2s_mode_t;

typedef enum {
    PORT_I2S_STANDARD_PHILIPS = 0,
    PORT_I2S_STANDARD_MSB,
    PORT_I2S_STANDARD_LSB,
    PORT_I2S_STANDARD_PCM
} port_i2s_standard_t;

typedef enum {
    PORT_I2S_DATA_16BIT = 0,
    PORT_I2S_DATA_24BIT,
    PORT_I2S_DATA_32BIT
} port_i2s_data_size_t;

typedef struct {
    port_i2s_mode_t mode;
    port_i2s_standard_t standard;

    uint32_t sample_rate;
    port_i2s_data_size_t data_size;

    bool master_clock;
} port_i2s_config_t;

typedef struct port_i2s_handle {
    port_handle_base_t base;
    port_i2s_config_t config;

    port_dma_handle_t *tx_dma;
    port_dma_handle_t *rx_dma;

    port_callback_t callback;
    void *user_data;
} port_i2s_handle_t;

port_status_t port_i2s_init(port_i2s_handle_t *handle, const port_i2s_config_t *config);
port_status_t port_i2s_deinit(port_i2s_handle_t *handle);

port_status_t port_i2s_transmit(port_i2s_handle_t *handle, const void *data,
                                size_t length, uint32_t timeout);
port_status_t port_i2s_receive(port_i2s_handle_t *handle, void *data,
                               size_t length, uint32_t timeout);

port_status_t port_i2s_transmit_dma(port_i2s_handle_t *handle, const void *data, size_t length);
port_status_t port_i2s_receive_dma(port_i2s_handle_t *handle, void *data, size_t length);

port_status_t port_i2s_abort(port_i2s_handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* PORT_I2S_H */
