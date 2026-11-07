#ifndef PORT_I2C_H
#define PORT_I2C_H

#include "port_def.h"
#include "port_dma.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PORT_I2C_MODE_MASTER = 0,
    PORT_I2C_MODE_SLAVE
} port_i2c_mode_t;

typedef enum {
    PORT_I2C_SPEED_STANDARD = 0,
    PORT_I2C_SPEED_FAST,
    PORT_I2C_SPEED_FAST_PLUS
} port_i2c_speed_t;

typedef enum {
    PORT_I2C_ADDR_7BIT = 0,
    PORT_I2C_ADDR_10BIT
} port_i2c_addr_mode_t;

typedef struct {
    port_i2c_mode_t mode;
    port_i2c_speed_t speed;
    port_i2c_addr_mode_t address_mode;

    uint16_t own_address;
} port_i2c_config_t;

typedef struct port_i2c_handle {
    port_handle_base_t base;

    port_i2c_config_t config;

    port_dma_handle_t *tx_dma;
    port_dma_handle_t *rx_dma;

    port_callback_t callback;
    void *user_data;
} port_i2c_handle_t;

port_status_t port_i2c_init(port_i2c_handle_t *handle, const port_i2c_config_t *config);
port_status_t port_i2c_deinit(port_i2c_handle_t *handle);

/* 主机接口 */
port_status_t port_i2c_master_transmit(port_i2c_handle_t *handle, uint16_t address,
                                       const uint8_t *data, size_t length, uint32_t timeout);
port_status_t port_i2c_master_receive(port_i2c_handle_t *handle, uint16_t address,
                                      uint8_t *data, size_t length, uint32_t timeout);

/* 组合传输，支持 Repeated Start */
port_status_t port_i2c_master_transfer(port_i2c_handle_t *handle, uint16_t address,
                                       const uint8_t *tx_data, size_t tx_length,
                                       uint8_t *rx_data, size_t rx_length, uint32_t timeout);

/* 寄存器访问 */
port_status_t port_i2c_mem_write(port_i2c_handle_t *handle, uint16_t address,
                                 uint16_t reg, uint8_t reg_size,
                                 const uint8_t *data, size_t length, uint32_t timeout);
port_status_t port_i2c_mem_read(port_i2c_handle_t *handle, uint16_t address,
                                uint16_t reg, uint8_t reg_size,
                                uint8_t *data, size_t length, uint32_t timeout);

/* DMA */
port_status_t port_i2c_master_transmit_dma(port_i2c_handle_t *handle, uint16_t address,
                                           const uint8_t *data, size_t length);
port_status_t port_i2c_master_receive_dma(port_i2c_handle_t *handle, uint16_t address,
                                          uint8_t *data, size_t length);

/* 总线管理 */
port_status_t port_i2c_is_device_ready(port_i2c_handle_t *handle, uint16_t address,
                                       uint32_t trials, uint32_t timeout);
port_status_t port_i2c_bus_recover(port_i2c_handle_t *handle);
port_status_t port_i2c_abort(port_i2c_handle_t *handle);

port_state_t port_i2c_get_state(port_i2c_handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* PORT_I2C_H */
