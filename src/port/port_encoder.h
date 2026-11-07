#ifndef PORT_ENCODER_H
#define PORT_ENCODER_H

#include "port_def.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PORT_ENCODER_MODE_TI1 = 0,
    PORT_ENCODER_MODE_TI2,
    PORT_ENCODER_MODE_TI12
} port_encoder_mode_t;

typedef struct {
    port_encoder_mode_t mode;

    uint32_t period;
    uint32_t filter;

    bool reverse;
} port_encoder_config_t;

typedef struct port_encoder_handle {
    port_handle_base_t base;
    port_encoder_config_t config;
} port_encoder_handle_t;

port_status_t port_encoder_init(port_encoder_handle_t *handle,
                                const port_encoder_config_t *config);
port_status_t port_encoder_start(port_encoder_handle_t *handle);
port_status_t port_encoder_stop(port_encoder_handle_t *handle);

int32_t port_encoder_get_count(port_encoder_handle_t *handle);

port_status_t port_encoder_set_count(port_encoder_handle_t *handle, int32_t count);

#ifdef __cplusplus
}
#endif

#endif /* PORT_ENCODER_H */
