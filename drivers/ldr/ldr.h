#ifndef LDR_H
#define LDR_H

/* LDR 光敏电阻（分压模拟输出），亮度越大电阻越小、电压越低，返回原始电压。 */
#include <stdint.h>
#include "bus/adc.h"

#ifdef __cplusplus
extern "C" {
#endif

int ldr_init(const adc_bus_t *bus);
int ldr_read(const adc_bus_t *bus, uint16_t *raw);

#ifdef __cplusplus
}
#endif

#endif /* LDR_H */