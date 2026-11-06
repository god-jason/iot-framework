#ifndef IOT_SENSOR_MQ4_H
#define IOT_SENSOR_MQ4_H

/* MQ-4 甲烷（天然气）传感器，模拟电压输出接 ADC，读取电压近似换算浓度。 */
#include <stdint.h>
#include "bus/iot_bus_adc.h"

#ifdef __cplusplus
extern "C" {
#endif

int iot_mq4_init(const iot_adc_bus_t *bus);
int iot_mq4_read(const iot_adc_bus_t *bus, uint32_t *ppm);

#ifdef __cplusplus
}
#endif

#endif /* IOT_SENSOR_MQ4_H */