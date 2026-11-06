#ifndef IOT_SENSOR_GP2Y1010AU0F_H
#define IOT_SENSOR_GP2Y1010AU0F_H

/* GP2Y1010AU0F 灰尘传感器（模拟输出 + LED 脉冲），简化为读取模拟电压映射浓度。 */
#include <stdint.h>
#include "bus/iot_bus_adc.h"

#ifdef __cplusplus
extern "C" {
#endif

int iot_gp2y1010_read(const iot_adc_bus_t *bus, float *density);

#ifdef __cplusplus
}
#endif

#endif /* IOT_SENSOR_GP2Y1010AU0F_H */