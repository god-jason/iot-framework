#ifndef YL69_H
#define YL69_H

/* YL-69 土壤湿度传感器（模拟输出，电阻分压），返回原始电压。 */
#include <stdint.h>
#include "bus/adc.h"

#ifdef __cplusplus
extern "C" {
#endif

int yl69_init(const adc_bus_t *bus);
int yl69_read(const adc_bus_t *bus, uint16_t *raw);

#ifdef __cplusplus
}
#endif

#endif /* YL69_H */