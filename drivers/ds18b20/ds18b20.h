#ifndef DS18B20_H
#define DS18B20_H

/* DS18B20 单总线温度传感器（-55~+125 摄氏度，12 位）。 */
#include "bus/onewire.h"

#ifdef __cplusplus
extern "C" {
#endif

int ds18b20_start(const onewire_bus_t *bus);
int ds18b20_read(const onewire_bus_t *bus, float *celsius);

#ifdef __cplusplus
}
#endif

#endif /* DS18B20_H */