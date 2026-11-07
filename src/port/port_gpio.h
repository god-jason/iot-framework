#ifndef PORT_GPIO_H
#define PORT_GPIO_H

#include "port_def.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PORT_GPIO_LOW = 0,
    PORT_GPIO_HIGH
} port_gpio_level_t;

typedef enum {
    PORT_GPIO_MODE_INPUT = 0,
    PORT_GPIO_MODE_OUTPUT,
    PORT_GPIO_MODE_ANALOG,
    PORT_GPIO_MODE_AF
} port_gpio_mode_t;

typedef enum {
    PORT_GPIO_OUTPUT_PP = 0,
    PORT_GPIO_OUTPUT_OD
} port_gpio_output_t;

typedef enum {
    PORT_GPIO_NOPULL = 0,
    PORT_GPIO_PULLUP,
    PORT_GPIO_PULLDOWN
} port_gpio_pull_t;

typedef enum {
    PORT_GPIO_SPEED_LOW = 0,
    PORT_GPIO_SPEED_MEDIUM,
    PORT_GPIO_SPEED_HIGH,
    PORT_GPIO_SPEED_VERY_HIGH
} port_gpio_speed_t;

typedef struct {
    port_gpio_mode_t mode;
    port_gpio_output_t output;
    port_gpio_pull_t pull;
    port_gpio_speed_t speed;
    uint8_t alternate;
    port_gpio_level_t initial_level;
} port_gpio_config_t;

port_status_t port_gpio_init(port_pin_t pin, const port_gpio_config_t *config);
port_status_t port_gpio_deinit(port_pin_t pin);

port_status_t port_gpio_write(port_pin_t pin, port_gpio_level_t level);
port_status_t port_gpio_read(port_pin_t pin, port_gpio_level_t *level);
port_status_t port_gpio_toggle(port_pin_t pin);

port_status_t port_gpio_set(port_pin_t pin);
port_status_t port_gpio_reset(port_pin_t pin);

#ifdef __cplusplus
}
#endif

#endif /* PORT_GPIO_H */
