#ifndef IOT_SENSOR_YL83_H
#define IOT_SENSOR_YL83_H

/* YL-83 雨滴检测传感器（FC-37 同类，模拟输出），返回原始电压。 */
#include <stdint.h>
#include "bus/iot_bus_adc.h"

#ifdef __cplusplus
extern "C" {
#endif

int iot_yl83_init(const iot_adc_bus_t *bus);
int iot_yl83_read(const iot_adc_bus_t *bus, uint16_t *raw);

#ifdef __cplusplus
}
#endif

#endif /* IOT_SENSOR_YL83_H */