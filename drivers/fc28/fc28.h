#ifndef FC28_H
#define FC28_H

/* FC-28 土壤湿度传感器（YL-69 同类，模拟输出），返回原始电压。 */
#include <stdint.h>
#include "bus/adc.h"

#ifdef __cplusplus
extern "C" {
#endif

int fc28_init(const adc_bus_t *bus);
int fc28_read(const adc_bus_t *bus, uint16_t *raw);

#ifdef __cplusplus
}
#endif

#endif /* FC28_H */