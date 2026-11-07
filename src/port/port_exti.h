#ifndef PORT_EXTI_H
#define PORT_EXTI_H

#include "port_def.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PORT_EXTI_RISING = 0,
    PORT_EXTI_FALLING,
    PORT_EXTI_BOTH
} port_exti_trigger_t;

typedef void (*port_exti_callback_t)(port_pin_t pin, void *user_data);

typedef struct {
    port_exti_trigger_t trigger;
    uint8_t priority;

    port_exti_callback_t callback;
    void *user_data;
} port_exti_config_t;

port_status_t port_exti_init(port_pin_t pin, const port_exti_config_t *config);
port_status_t port_exti_enable(port_pin_t pin);
port_status_t port_exti_disable(port_pin_t pin);
port_status_t port_exti_clear(port_pin_t pin);
port_status_t port_exti_deinit(port_pin_t pin);

#ifdef __cplusplus
}
#endif

#endif /* PORT_EXTI_H */
