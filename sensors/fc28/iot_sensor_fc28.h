#ifndef IOT_SENSOR_FC28_H
#define IOT_SENSOR_FC28_H

/* FC-28 土壤湿度传感器（YL-69 同类，模拟输出），返回原始电压。 */
#include <stdint.h>
#include "bus/iot_bus_adc.h"

#ifdef __cplusplus
extern "C" {
#endif

int iot_fc28_init(const iot_adc_bus_t *bus);
int iot_fc28_read(const iot_adc_bus_t *bus, uint16_t *raw);

#ifdef __cplusplus
}
#endif

#endif /* IOT_SENSOR_FC28_H */