#ifndef IOT_SENSOR_FC37_H
#define IOT_SENSOR_FC37_H

/* FC-37 雨滴检测传感器（模拟输出），返回原始电压。 */
#include <stdint.h>
#include "bus/iot_bus_adc.h"

#ifdef __cplusplus
extern "C" {
#endif

int iot_fc37_init(const iot_adc_bus_t *bus);
int iot_fc37_read(const iot_adc_bus_t *bus, uint16_t *raw);

#ifdef __cplusplus
}
#endif

#endif /* IOT_SENSOR_FC37_H */