#ifndef PORT_COMP_H
#define PORT_COMP_H

#include "port_def.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PORT_COMP_POLARITY_NORMAL = 0,
    PORT_COMP_POLARITY_INVERTED
} port_comp_polarity_t;

typedef struct {
    uint8_t positive_input;
    uint8_t negative_input;
    port_comp_polarity_t polarity;
} port_comp_config_t;

typedef struct port_comp_handle {
    port_handle_base_t base;
    port_comp_config_t config;
    port_callback_t callback;
    void *user_data;
} port_comp_handle_t;

port_status_t port_comp_init(port_comp_handle_t *handle, const port_comp_config_t *config);
port_status_t port_comp_start(port_comp_handle_t *handle);
port_status_t port_comp_stop(port_comp_handle_t *handle);
port_status_t port_comp_read(port_comp_handle_t *handle, bool *value);

#ifdef __cplusplus
}
#endif

#endif /* PORT_COMP_H */
