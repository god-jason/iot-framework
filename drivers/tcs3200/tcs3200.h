#ifndef TCS3200_H
#define TCS3200_H

/* TCS3200 颜色传感器（S0/S1 分频 + S2/S3 滤波选择 + OUT 频率输出）。 */
#include <stdint.h>
#include "bus/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

int tcs3200_init(gpio_bus_t *bus, uint8_t s0, uint8_t s1, uint8_t s2, uint8_t s3, uint8_t out);
int tcs3200_read_color(gpio_bus_t *bus, uint8_t s0, uint8_t s1, uint8_t s2, uint8_t s3,
                           uint8_t out, uint32_t *r, uint32_t *g, uint32_t *b);

#ifdef __cplusplus
}
#endif

#endif /* TCS3200_H */