#ifndef MQ135_H
#define MQ135_H

/* MQ-135 空气质量传感器（氨气/苯系物/烟雾等），模拟电压输出接 ADC，读取电压近似换算浓度。 */
#include <stdint.h>
#include "bus/adc.h"

#ifdef __cplusplus
extern "C" {
#endif

int mq135_init(const adc_bus_t *bus);
int mq135_read(const adc_bus_t *bus, uint32_t *ppm);

#ifdef __cplusplus
}
#endif

#endif /* MQ135_H */