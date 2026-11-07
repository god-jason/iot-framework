#ifndef FSR_H
#define FSR_H

/* FSR 薄膜压力电阻传感器（电阻分压模拟输出），返回原始电压。 */
#include <stdint.h>
#include "bus/adc.h"

#ifdef __cplusplus
extern "C" {
#endif

int fsr_init(const adc_bus_t *bus);
int fsr_read(const adc_bus_t *bus, uint16_t *raw);

#ifdef __cplusplus
}
#endif

#endif /* FSR_H */