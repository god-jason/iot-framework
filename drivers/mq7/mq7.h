#ifndef MQ7_H
#define MQ7_H

/* MQ-7 一氧化碳（CO）传感器，模拟电压输出接 ADC，读取电压近似换算浓度。 */
#include <stdint.h>
#include "bus/adc.h"

#ifdef __cplusplus
extern "C" {
#endif

int mq7_init(const adc_bus_t *bus);
int mq7_read(const adc_bus_t *bus, uint32_t *ppm);

#ifdef __cplusplus
}
#endif

#endif /* MQ7_H */