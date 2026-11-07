#ifndef RELAY_H
#define RELAY_H

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
} relay_t;

int relay_init(relay_t *relay, uint8_t pin, uint8_t active_level);
int relay_on(relay_t *relay);
int relay_off(relay_t *relay);
int relay_toggle(relay_t *relay);

#ifdef __cplusplus
}
#endif

#endif /* RELAY_H */