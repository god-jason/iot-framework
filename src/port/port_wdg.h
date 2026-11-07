#ifndef PORT_WDG_H
#define PORT_WDG_H

#include "port_def.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PORT_WDG_INDEPENDENT = 0,
    PORT_WDG_WINDOW
} port_wdg_type_t;

typedef struct {
    port_wdg_type_t type;
    uint32_t timeout_ms;
    uint32_t window_ms;
} port_wdg_config_t;

typedef struct port_wdg_handle {
    port_handle_base_t base;
    port_wdg_config_t config;
} port_wdg_handle_t;

port_status_t port_wdg_init(port_wdg_handle_t *handle, const port_wdg_config_t *config);
port_status_t port_wdg_start(port_wdg_handle_t *handle);
port_status_t port_wdg_refresh(port_wdg_handle_t *handle);
port_status_t port_wdg_stop(port_wdg_handle_t *handle);

bool port_wdg_is_reset(void);
void port_wdg_clear_reset_flag(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_WDG_H */
