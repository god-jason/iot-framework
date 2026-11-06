#ifndef IOT_SENSOR_YL69_H
#define IOT_SENSOR_YL69_H

/* YL-69 土壤湿度传感器（模拟输出，电阻分压），返回原始电压。 */
#include <stdint.h>
#include "bus/iot_bus_adc.h"

#ifdef __cplusplus
extern "C" {
#endif

int iot_yl69_init(const iot_adc_bus_t *bus);
int iot_yl69_read(const iot_adc_bus_t *bus, uint16_t *raw);

#ifdef __cplusplus
}
#endif

#endif /* IOT_SENSOR_YL69_H */