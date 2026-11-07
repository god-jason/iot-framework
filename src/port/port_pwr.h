#ifndef PORT_PWR_H
#define PORT_PWR_H

#include "port_def.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PORT_PWR_MODE_SLEEP = 0,
    PORT_PWR_MODE_STOP,
    PORT_PWR_MODE_STANDBY
} port_pwr_mode_t;

typedef enum {
    PORT_PWR_WAKEUP_GPIO = 0,
    PORT_PWR_WAKEUP_RTC,
    PORT_PWR_WAKEUP_WDG,
    PORT_PWR_WAKEUP_OTHER
} port_pwr_wakeup_t;

typedef struct {
    port_pwr_mode_t mode;

    uint32_t wakeup_sources;

    bool low_power_regulator;
} port_pwr_config_t;

typedef struct {
    uint32_t reset_flags;
    uint32_t wakeup_flags;
} port_pwr_status_t;

port_status_t port_pwr_init(void);

port_status_t port_pwr_enter(const port_pwr_config_t *config);

port_status_t port_pwr_get_status(port_pwr_status_t *status);

port_status_t port_pwr_clear_flags(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_PWR_H */
