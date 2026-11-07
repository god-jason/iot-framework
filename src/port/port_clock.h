#ifndef PORT_CLOCK_H
#define PORT_CLOCK_H

#include "port_def.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PORT_CLOCK_CORE = 0,
    PORT_CLOCK_AHB,
    PORT_CLOCK_APB1,
    PORT_CLOCK_APB2,
    PORT_CLOCK_APB3,
    PORT_CLOCK_APB4
} port_clock_bus_t;

typedef enum {
    PORT_CLOCK_HSI = 0,
    PORT_CLOCK_HSE,
    PORT_CLOCK_PLL,
    PORT_CLOCK_LSI,
    PORT_CLOCK_LSE
} port_clock_source_t;

typedef struct {
    port_clock_source_t source;
    uint32_t frequency;
    uint32_t pll_frequency;
} port_clock_config_t;

port_status_t port_clock_init(const port_clock_config_t *config);
uint32_t port_clock_get_frequency(port_clock_bus_t bus);
port_status_t port_clock_enable(void *peripheral);
port_status_t port_clock_disable(void *peripheral);

#ifdef __cplusplus
}
#endif

#endif /* PORT_CLOCK_H */
