#ifndef GP2Y1010AU0F_H
#define GP2Y1010AU0F_H

/* GP2Y1010AU0F 灰尘传感器（模拟输出 + LED 脉冲），简化为读取模拟电压映射浓度。 */
#include <stdint.h>
#include "bus/adc.h"

#ifdef __cplusplus
extern "C" {
#endif

int gp2y1010_read(const adc_bus_t *bus, float *density);

#ifdef __cplusplus
}
#endif

#endif /* GP2Y1010AU0F_H */