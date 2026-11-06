#ifndef IOT_SENSOR_MICS6814_H
#define IOT_SENSOR_MICS6814_H

/* MiCS-6814 多气体传感器（CO/NH3/NO2），三路模拟电压输出接 ADC。 */
#include <stdint.h>
#include "bus/iot_bus_adc.h"

#ifdef __cplusplus
extern "C" {
#endif

int iot_mics6814_init(const iot_adc_bus_t *bus);
int iot_mics6814_read(const iot_adc_bus_t *bus, uint16_t *ch);

#ifdef __cplusplus
}
#endif

#endif /* IOT_SENSOR_MICS6814_H */