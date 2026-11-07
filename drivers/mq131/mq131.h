#ifndef MQ131_H
#define MQ131_H

/* MQ-131 臭氧（O3）传感器，模拟电压输出接 ADC，读取电压近似换算浓度。 */
#include <stdint.h>
#include "bus/adc.h"

#ifdef __cplusplus
extern "C" {
#endif

int mq131_init(const adc_bus_t *bus);
int mq131_read(const adc_bus_t *bus, uint32_t *ppm);

#ifdef __cplusplus
}
#endif

#endif /* MQ131_H */