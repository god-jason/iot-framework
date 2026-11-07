#ifndef MQ2_H
#define MQ2_H

/* MQ-2 可燃气体传感器（LPG/甲烷/氢气/烟雾），模拟电压输出接 ADC，读取电压近似换算浓度。 */
#include <stdint.h>
#include "bus/adc.h"

#ifdef __cplusplus
extern "C" {
#endif

int mq2_init(const adc_bus_t *bus);
int mq2_read(const adc_bus_t *bus, uint32_t *ppm);

#ifdef __cplusplus
}
#endif

#endif /* MQ2_H */