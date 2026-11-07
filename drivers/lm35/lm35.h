#ifndef LM35_H
#define LM35_H

/* LM35 温度传感器（10mV/℃），模拟电压输出接 ADC。 */
#include <stdint.h>
#include "bus/adc.h"

#ifdef __cplusplus
extern "C" {
#endif

int lm35_init(const adc_bus_t *bus);
int lm35_read(const adc_bus_t *bus, float *celsius);

#ifdef __cplusplus
}
#endif

#endif /* LM35_H */