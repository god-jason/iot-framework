#ifndef PORT_CAPTURE_H
#define PORT_CAPTURE_H

#include "port_def.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PORT_CAPTURE_RISING = 0,
    PORT_CAPTURE_FALLING,
    PORT_CAPTURE_BOTH
} port_capture_edge_t;

typedef struct {
    port_capture_edge_t edge;
    uint32_t prescaler;
    uint32_t filter;
} port_capture_config_t;

typedef struct port_capture_handle {
    port_handle_base_t base;

    port_capture_config_t config;

    port_callback_t callback;
    void *user_data;
} port_capture_handle_t;

port_status_t port_capture_init(port_capture_handle_t *handle,
                                const port_capture_config_t *config);
port_status_t port_capture_start(port_capture_handle_t *handle);
port_status_t port_capture_stop(port_capture_handle_t *handle);
port_status_t port_capture_start_it(port_capture_handle_t *handle);

uint32_t port_capture_get_value(port_capture_handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* PORT_CAPTURE_H */
