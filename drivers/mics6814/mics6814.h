#ifndef MICS6814_H
#define MICS6814_H

/* MiCS-6814 多气体传感器（CO/NH3/NO2），三路模拟电压输出接 ADC。 */
#include <stdint.h>
#include "bus/adc.h"

#ifdef __cplusplus
extern "C" {
#endif

int mics6814_init(const adc_bus_t *bus);
int mics6814_read(const adc_bus_t *bus, uint16_t *ch);

#ifdef __cplusplus
}
#endif

#endif /* MICS6814_H */