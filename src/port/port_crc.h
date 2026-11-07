#ifndef PORT_CRC_H
#define PORT_CRC_H

#include "port_def.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PORT_CRC_WIDTH_8 = 8,
    PORT_CRC_WIDTH_16 = 16,
    PORT_CRC_WIDTH_32 = 32
} port_crc_width_t;

typedef struct {
    port_crc_width_t width;

    uint32_t polynomial;
    uint32_t initial_value;

    bool input_reverse;
    bool output_reverse;

    uint32_t xor_output;
} port_crc_config_t;

typedef struct port_crc_handle {
    port_handle_base_t base;
    port_crc_config_t config;
} port_crc_handle_t;

port_status_t port_crc_init(port_crc_handle_t *handle, const port_crc_config_t *config);
port_status_t port_crc_reset(port_crc_handle_t *handle);
port_status_t port_crc_calculate(port_crc_handle_t *handle, const void *data,
                                 size_t length, uint32_t *result);
port_status_t port_crc_deinit(port_crc_handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* PORT_CRC_H */
