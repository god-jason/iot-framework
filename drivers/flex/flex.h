#ifndef FLEX_H
#define FLEX_H

/* 弯曲传感器（电阻分压模拟输出），返回原始电压。 */
#include <stdint.h>
#include "bus/adc.h"

#ifdef __cplusplus
extern "C" {
#endif

int flex_init(const adc_bus_t *bus);
int flex_read(const adc_bus_t *bus, uint16_t *raw);

#ifdef __cplusplus
}
#endif

#endif /* FLEX_H */