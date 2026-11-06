#ifndef IOT_SENSOR_ACS712_H
#define IOT_SENSOR_ACS712_H

/* ACS712 霍尔电流传感器（零电流 2.5V 基准），模拟电压输出接 ADC。 */
#include <stdint.h>
#include "bus/iot_bus_adc.h"

#ifdef __cplusplus
extern "C" {
#endif

int iot_acs712_init(const iot_adc_bus_t *bus);
int iot_acs712_read(const iot_adc_bus_t *bus, float *amps);

#ifdef __cplusplus
}
#endif

#endif /* IOT_SENSOR_ACS712_H */