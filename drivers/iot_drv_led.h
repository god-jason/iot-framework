#ifndef IOT_DRV_LED_H
#define IOT_DRV_LED_H

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
} iot_led_t;

int iot_led_init(iot_led_t *led, uint8_t pin, uint8_t active_level);
int iot_led_on(iot_led_t *led);
int iot_led_off(iot_led_t *led);
int iot_led_toggle(iot_led_t *led);

#ifdef __cplusplus
}
#endif

#endif /* IOT_DRV_LED_H */