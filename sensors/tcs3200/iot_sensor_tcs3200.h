#ifndef IOT_SENSOR_TCS3200_H
#define IOT_SENSOR_TCS3200_H

/* TCS3200 颜色传感器（S0/S1 分频 + S2/S3 滤波选择 + OUT 频率输出）。 */
#include <stdint.h>
#include "bus/iot_bus_gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

int iot_tcs3200_init(iot_gpio_bus_t *bus, uint8_t s0, uint8_t s1, uint8_t s2, uint8_t s3, uint8_t out);
int iot_tcs3200_read_color(iot_gpio_bus_t *bus, uint8_t s0, uint8_t s1, uint8_t s2, uint8_t s3,
                           uint8_t out, uint32_t *r, uint32_t *g, uint32_t *b);

#ifdef __cplusplus
}
#endif

#endif /* IOT_SENSOR_TCS3200_H */