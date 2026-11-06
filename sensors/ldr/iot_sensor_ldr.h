#ifndef IOT_SENSOR_LDR_H
#define IOT_SENSOR_LDR_H

/* LDR 光敏电阻（分压模拟输出），亮度越大电阻越小、电压越低，返回原始电压。 */
#include <stdint.h>
#include "bus/iot_bus_adc.h"

#ifdef __cplusplus
extern "C" {
#endif

int iot_ldr_init(const iot_adc_bus_t *bus);
int iot_ldr_read(const iot_adc_bus_t *bus, uint16_t *raw);

#ifdef __cplusplus
}
#endif

#endif /* IOT_SENSOR_LDR_H */