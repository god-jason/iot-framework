#ifndef IOT_SENSOR_FLEX_H
#define IOT_SENSOR_FLEX_H

/* 弯曲传感器（电阻分压模拟输出），返回原始电压。 */
#include <stdint.h>
#include "bus/iot_bus_adc.h"

#ifdef __cplusplus
extern "C" {
#endif

int iot_flex_init(const iot_adc_bus_t *bus);
int iot_flex_read(const iot_adc_bus_t *bus, uint16_t *raw);

#ifdef __cplusplus
}
#endif

#endif /* IOT_SENSOR_FLEX_H */