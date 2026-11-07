#ifndef FC37_H
#define FC37_H

/* FC-37 雨滴检测传感器（模拟输出），返回原始电压。 */
#include <stdint.h>
#include "bus/adc.h"

#ifdef __cplusplus
extern "C" {
#endif

int fc37_init(const adc_bus_t *bus);
int fc37_read(const adc_bus_t *bus, uint16_t *raw);

#ifdef __cplusplus
}
#endif

#endif /* FC37_H */