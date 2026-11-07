#ifndef CAPSOIL_H
#define CAPSOIL_H

/* 电容式土壤湿度传感器 v1.2（模拟输出），返回原始电压。 */
#include <stdint.h>
#include "bus/adc.h"

#ifdef __cplusplus
extern "C" {
#endif

int capsoil_init(const adc_bus_t *bus);
int capsoil_read(const adc_bus_t *bus, uint16_t *raw);

#ifdef __cplusplus
}
#endif

#endif /* CAPSOIL_H */