#ifndef PORT_RNG_H
#define PORT_RNG_H

#include "port_def.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct port_rng_handle {
    port_handle_base_t base;
} port_rng_handle_t;

port_status_t port_rng_init(port_rng_handle_t *handle);
port_status_t port_rng_deinit(port_rng_handle_t *handle);

port_status_t port_rng_get(port_rng_handle_t *handle, uint32_t *value);
port_status_t port_rng_generate(port_rng_handle_t *handle, void *buffer, size_t length);

#ifdef __cplusplus
}
#endif

#endif /* PORT_RNG_H */
