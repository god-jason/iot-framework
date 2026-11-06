#ifndef IOT_DRV_RELAY_H
#define IOT_DRV_RELAY_H

/*
 * 继电器驱动（GPIO）。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t pin;
    uint8_t active_level;
} iot_relay_t;

int iot_relay_init(iot_relay_t *relay, uint8_t pin, uint8_t active_level);
int iot_relay_on(iot_relay_t *relay);
int iot_relay_off(iot_relay_t *relay);
int iot_relay_toggle(iot_relay_t *relay);

#ifdef __cplusplus
}
#endif

#endif /* IOT_DRV_RELAY_H */