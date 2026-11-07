#ifndef TMP36_H
#define TMP36_H

/* TMP36 温度传感器（10mV/℃，0℃ 时输出 500mV），模拟电压输出接 ADC。 */
#include <stdint.h>
#include "bus/adc.h"

#ifdef __cplusplus
extern "C" {
#endif

int tmp36_init(const adc_bus_t *bus);
int tmp36_read(const adc_bus_t *bus, float *celsius);

#ifdef __cplusplus
}
#endif

#endif /* TMP36_H */