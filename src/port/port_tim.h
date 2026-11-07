#ifndef PORT_TIM_H
#define PORT_TIM_H

#include "port_def.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PORT_TIM_COUNTER_UP = 0,
    PORT_TIM_COUNTER_DOWN,
    PORT_TIM_COUNTER_CENTER
} port_tim_counter_t;

typedef enum {
    PORT_TIM_EVENT_PERIOD = 0,
    PORT_TIM_EVENT_COMPARE,
    PORT_TIM_EVENT_CAPTURE,
    PORT_TIM_EVENT_UPDATE
} port_tim_event_t;

typedef struct {
    uint32_t prescaler;
    uint32_t period;

    port_tim_counter_t counter_mode;

    uint8_t clock_division;
} port_tim_config_t;

typedef struct port_tim_handle {
    port_handle_base_t base;

    port_tim_config_t config;

    port_callback_t callback;
    void *user_data;
} port_tim_handle_t;

port_status_t port_tim_init(port_tim_handle_t *handle, const port_tim_config_t *config);
port_status_t port_tim_deinit(port_tim_handle_t *handle);

port_status_t port_tim_start(port_tim_handle_t *handle);
port_status_t port_tim_stop(port_tim_handle_t *handle);

port_status_t port_tim_start_it(port_tim_handle_t *handle);
port_status_t port_tim_stop_it(port_tim_handle_t *handle);

port_status_t port_tim_set_period(port_tim_handle_t *handle, uint32_t period);
port_status_t port_tim_set_prescaler(port_tim_handle_t *handle, uint32_t prescaler);
port_status_t port_tim_set_counter(port_tim_handle_t *handle, uint32_t counter);

uint32_t port_tim_get_counter(port_tim_handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* PORT_TIM_H */
