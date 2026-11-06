#ifndef IOT_SENSOR_LM35_H
#define IOT_SENSOR_LM35_H

/* LM35 温度传感器（10mV/℃），模拟电压输出接 ADC。 */
#include <stdint.h>
#include "bus/iot_bus_adc.h"

#ifdef __cplusplus
extern "C" {
#endif

int iot_lm35_init(const iot_adc_bus_t *bus);
int iot_lm35_read(const iot_adc_bus_t *bus, float *celsius);

#ifdef __cplusplus
}
#endif

#endif /* IOT_SENSOR_LM35_H */