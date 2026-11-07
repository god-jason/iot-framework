#ifndef PORT_PIN_H
#define PORT_PIN_H

/*
 * 引脚复用配置。GPIO 电气属性由 port_gpio 管理。
 */

#include "port_def.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PORT_PIN_FUNCTION_GPIO = 0,
    PORT_PIN_FUNCTION_AF,
    PORT_PIN_FUNCTION_ANALOG
} port_pin_function_t;

typedef struct {
    port_pin_t pin;
    port_pin_function_t function;
    uint8_t alternate;
} port_pin_config_t;

port_status_t port_pin_init(const port_pin_config_t *config);
port_status_t port_pin_deinit(port_pin_t pin);

#ifdef __cplusplus
}
#endif

#endif /* PORT_PIN_H */
