#ifndef LED_H
#define LED_H

/*
 * LED 驱动（GPIO）。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t pin;
    uint8_t active_level;   /* 点亮电平 0/1 */
    uint8_t state;
} led_t;

int led_init(led_t *led, uint8_t pin, uint8_t active_level);
int led_on(led_t *led);
int led_off(led_t *led);
int led_toggle(led_t *led);

#ifdef __cplusplus
}
#endif

#endif /* LED_H */