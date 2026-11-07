#ifndef YL83_H
#define YL83_H

/* YL-83 雨滴检测传感器（FC-37 同类，模拟输出），返回原始电压。 */
#include <stdint.h>
#include "bus/adc.h"

#ifdef __cplusplus
extern "C" {
#endif

int yl83_init(const adc_bus_t *bus);
int yl83_read(const adc_bus_t *bus, uint16_t *raw);

#ifdef __cplusplus
}
#endif

#endif /* YL83_H */