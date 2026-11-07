#ifndef PORT_ETH_H
#define PORT_ETH_H

#include "port_def.h"
#include "port_dma.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t speed;
    bool full_duplex;
    bool checksum_offload;
} port_eth_config_t;

typedef struct port_eth_handle {
    port_handle_base_t base;
    port_eth_config_t config;

    port_dma_handle_t *rx_dma;
    port_dma_handle_t *tx_dma;

    port_callback_t callback;
    void *user_data;
} port_eth_handle_t;

port_status_t port_eth_init(port_eth_handle_t *handle, const port_eth_config_t *config);
port_status_t port_eth_deinit(port_eth_handle_t *handle);

port_status_t port_eth_start(port_eth_handle_t *handle);
port_status_t port_eth_stop(port_eth_handle_t *handle);

port_status_t port_eth_transmit(port_eth_handle_t *handle, const void *data, size_t length);
port_status_t port_eth_receive(port_eth_handle_t *handle, void *buffer,
                               size_t buffer_size, size_t *received_length);

#ifdef __cplusplus
}
#endif

#endif /* PORT_ETH_H */
