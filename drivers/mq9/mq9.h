#ifndef MQ9_H
#define MQ9_H

/* MQ-9 一氧化碳/可燃气体传感器，模拟电压输出接 ADC，读取电压近似换算浓度。 */
#include <stdint.h>
#include "bus/adc.h"

#ifdef __cplusplus
extern "C" {
#endif

int mq9_init(const adc_bus_t *bus);
int mq9_read(const adc_bus_t *bus, uint32_t *ppm);

#ifdef __cplusplus
}
#endif

#endif /* MQ9_H */