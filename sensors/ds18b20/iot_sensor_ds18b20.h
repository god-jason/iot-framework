#ifndef IOT_SENSOR_DS18B20_H
#define IOT_SENSOR_DS18B20_H

/* DS18B20 单总线温度传感器（-55~+125 摄氏度，12 位）。 */
#include "bus/iot_bus_onewire.h"

#ifdef __cplusplus
extern "C" {
#endif

int iot_ds18b20_start(const iot_onewire_bus_t *bus);
int iot_ds18b20_read(const iot_onewire_bus_t *bus, float *celsius);

#ifdef __cplusplus
}
#endif

#endif /* IOT_SENSOR_DS18B20_H */