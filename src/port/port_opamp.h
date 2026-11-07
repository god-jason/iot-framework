#ifndef PORT_OPAMP_H
#define PORT_OPAMP_H

#include "port_def.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PORT_OPAMP_MODE_FOLLOWER = 0,
    PORT_OPAMP_MODE_PGA
} port_opamp_mode_t;

typedef struct {
    port_opamp_mode_t mode;
    uint8_t gain;
    uint8_t positive_input;
    uint8_t negative_input;
} port_opamp_config_t;

typedef struct port_opamp_handle {
    port_handle_base_t base;
    port_opamp_config_t config;
} port_opamp_handle_t;

port_status_t port_opamp_init(port_opamp_handle_t *handle, const port_opamp_config_t *config);
port_status_t port_opamp_start(port_opamp_handle_t *handle);
port_status_t port_opamp_stop(port_opamp_handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* PORT_OPAMP_H */
