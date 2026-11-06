#ifndef IOT_SENSOR_FSR_H
#define IOT_SENSOR_FSR_H

/* FSR 薄膜压力电阻传感器（电阻分压模拟输出），返回原始电压。 */
#include <stdint.h>
#include "bus/iot_bus_adc.h"

#ifdef __cplusplus
extern "C" {
#endif

int iot_fsr_init(const iot_adc_bus_t *bus);
int iot_fsr_read(const iot_adc_bus_t *bus, uint16_t *raw);

#ifdef __cplusplus
}
#endif

#endif /* IOT_SENSOR_FSR_H */