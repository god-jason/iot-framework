#ifndef ACS712_H
#define ACS712_H

/* ACS712 霍尔电流传感器（零电流 2.5V 基准），模拟电压输出接 ADC。 */
#include <stdint.h>
#include "bus/adc.h"

#ifdef __cplusplus
extern "C" {
#endif

int acs712_init(const adc_bus_t *bus);
int acs712_read(const adc_bus_t *bus, float *amps);

#ifdef __cplusplus
}
#endif

#endif /* ACS712_H */