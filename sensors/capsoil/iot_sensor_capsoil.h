#ifndef IOT_SENSOR_CAPSOIL_H
#define IOT_SENSOR_CAPSOIL_H

/* 电容式土壤湿度传感器 v1.2（模拟输出），返回原始电压。 */
#include <stdint.h>
#include "bus/iot_bus_adc.h"

#ifdef __cplusplus
extern "C" {
#endif

int iot_capsoil_init(const iot_adc_bus_t *bus);
int iot_capsoil_read(const iot_adc_bus_t *bus, uint16_t *raw);

#ifdef __cplusplus
}
#endif

#endif /* IOT_SENSOR_CAPSOIL_H */