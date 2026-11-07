#ifndef PORT_QSPI_H
#define PORT_QSPI_H

#include "port_def.h"
#include "port_dma.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t clock_frequency;
    uint8_t flash_size;
    uint8_t clock_mode;
    bool dual_flash;
} port_qspi_config_t;

typedef struct {
    uint8_t instruction;
    uint8_t address_size;
    uint8_t data_lines;
    uint8_t address_lines;
    uint32_t address;
    size_t data_length;
    uint8_t dummy_cycles;
} port_qspi_command_t;

typedef struct port_qspi_handle {
    port_handle_base_t base;
    port_qspi_config_t config;
    port_dma_handle_t *dma;
    port_callback_t callback;
    void *user_data;
} port_qspi_handle_t;

port_status_t port_qspi_init(port_qspi_handle_t *handle, const port_qspi_config_t *config);
port_status_t port_qspi_deinit(port_qspi_handle_t *handle);

port_status_t port_qspi_command(port_qspi_handle_t *handle, const port_qspi_command_t *command);
port_status_t port_qspi_transmit(port_qspi_handle_t *handle, const void *data,
                                 size_t length, uint32_t timeout);
port_status_t port_qspi_receive(port_qspi_handle_t *handle, void *data,
                                size_t length, uint32_t timeout);

port_status_t port_qspi_memory_map(port_qspi_handle_t *handle);
port_status_t port_qspi_memory_unmap(port_qspi_handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* PORT_QSPI_H */
