#ifndef MQ4_H
#define MQ4_H

/* MQ-4 甲烷（天然气）传感器，模拟电压输出接 ADC，读取电压近似换算浓度。 */
#include <stdint.h>
#include "bus/adc.h"

#ifdef __cplusplus
extern "C" {
#endif

int mq4_init(const adc_bus_t *bus);
int mq4_read(const adc_bus_t *bus, uint32_t *ppm);

#ifdef __cplusplus
}
#endif

#endif /* MQ4_H */