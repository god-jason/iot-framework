#ifndef PORT_PWM_H
#define PORT_PWM_H

#include "port_def.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PORT_PWM_POLARITY_HIGH = 0,
    PORT_PWM_POLARITY_LOW
} port_pwm_polarity_t;

typedef struct {
    uint32_t frequency;
    uint32_t duty;              /* 0~10000，对应 0~100.00% */

    port_pwm_polarity_t polarity;
} port_pwm_config_t;

typedef struct port_pwm_handle {
    port_handle_base_t base;

    port_pwm_config_t config;

    uint8_t channel;
} port_pwm_handle_t;

port_status_t port_pwm_init(port_pwm_handle_t *handle, const port_pwm_config_t *config);
port_status_t port_pwm_deinit(port_pwm_handle_t *handle);

port_status_t port_pwm_start(port_pwm_handle_t *handle);
port_status_t port_pwm_stop(port_pwm_handle_t *handle);

port_status_t port_pwm_set_frequency(port_pwm_handle_t *handle, uint32_t frequency);
port_status_t port_pwm_set_duty(port_pwm_handle_t *handle, uint32_t duty);
port_status_t port_pwm_set_polarity(port_pwm_handle_t *handle, port_pwm_polarity_t polarity);

#ifdef __cplusplus
}
#endif

#endif /* PORT_PWM_H */
