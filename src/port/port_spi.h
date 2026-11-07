#ifndef PORT_SPI_H
#define PORT_SPI_H

#include "port_def.h"
#include "port_dma.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PORT_SPI_MODE_MASTER = 0,
    PORT_SPI_MODE_SLAVE
} port_spi_mode_t;

typedef enum {
    PORT_SPI_DIRECTION_2LINES = 0,
    PORT_SPI_DIRECTION_2LINES_RXONLY,
    PORT_SPI_DIRECTION_1LINE
} port_spi_direction_t;

typedef enum {
    PORT_SPI_CPOL_LOW = 0,
    PORT_SPI_CPOL_HIGH
} port_spi_cpol_t;

typedef enum {
    PORT_SPI_CPHA_1EDGE = 0,
    PORT_SPI_CPHA_2EDGE
} port_spi_cpha_t;

typedef enum {
    PORT_SPI_BITORDER_MSB = 0,
    PORT_SPI_BITORDER_LSB
} port_spi_bitorder_t;

typedef struct {
    port_spi_mode_t mode;
    port_spi_direction_t direction;

    uint32_t baudrate;

    port_spi_cpol_t cpol;
    port_spi_cpha_t cpha;
    port_spi_bitorder_t bit_order;

    uint8_t data_bits;
} port_spi_config_t;

typedef struct port_spi_handle {
    port_handle_base_t base;

    port_spi_config_t config;

    port_dma_handle_t *tx_dma;
    port_dma_handle_t *rx_dma;

    port_callback_t callback;
    void *user_data;
} port_spi_handle_t;

port_status_t port_spi_init(port_spi_handle_t *handle, const port_spi_config_t *config);
port_status_t port_spi_deinit(port_spi_handle_t *handle);

port_status_t port_spi_transmit(port_spi_handle_t *handle, const void *data,
                                size_t length, uint32_t timeout);
port_status_t port_spi_receive(port_spi_handle_t *handle, void *data,
                               size_t length, uint32_t timeout);
port_status_t port_spi_transfer(port_spi_handle_t *handle, const void *tx_data,
                                void *rx_data, size_t length, uint32_t timeout);

port_status_t port_spi_transmit_dma(port_spi_handle_t *handle, const void *data, size_t length);
port_status_t port_spi_receive_dma(port_spi_handle_t *handle, void *data, size_t length);
port_status_t port_spi_transfer_dma(port_spi_handle_t *handle, const void *tx_data,
                                    void *rx_data, size_t length);

port_status_t port_spi_set_speed(port_spi_handle_t *handle, uint32_t baudrate);
port_status_t port_spi_abort(port_spi_handle_t *handle);

port_state_t port_spi_get_state(port_spi_handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* PORT_SPI_H */
