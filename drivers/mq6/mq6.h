#ifndef MQ6_H
#define MQ6_H

/* MQ-6 LPG（液化石油气）传感器，模拟电压输出接 ADC，读取电压近似换算浓度。 */
#include <stdint.h>
#include "bus/adc.h"

#ifdef __cplusplus
extern "C" {
#endif

int mq6_init(const adc_bus_t *bus);
int mq6_read(const adc_bus_t *bus, uint32_t *ppm);

#ifdef __cplusplus
}
#endif

#endif /* MQ6_H */