#ifndef IOT_SENSOR_TMP36_H
#define IOT_SENSOR_TMP36_H

/* TMP36 温度传感器（10mV/℃，0℃ 时输出 500mV），模拟电压输出接 ADC。 */
#include <stdint.h>
#include "bus/iot_bus_adc.h"

#ifdef __cplusplus
extern "C" {
#endif

int iot_tmp36_init(const iot_adc_bus_t *bus);
int iot_tmp36_read(const iot_adc_bus_t *bus, float *celsius);

#ifdef __cplusplus
}
#endif

#endif /* IOT_SENSOR_TMP36_H */