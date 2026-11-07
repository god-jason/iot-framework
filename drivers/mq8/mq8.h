#ifndef MQ8_H
#define MQ8_H

/* MQ-8 氢气（H2）传感器，模拟电压输出接 ADC，读取电压近似换算浓度。 */
#include <stdint.h>
#include "bus/adc.h"

#ifdef __cplusplus
extern "C" {
#endif

int mq8_init(const adc_bus_t *bus);
int mq8_read(const adc_bus_t *bus, uint32_t *ppm);

#ifdef __cplusplus
}
#endif

#endif /* MQ8_H */