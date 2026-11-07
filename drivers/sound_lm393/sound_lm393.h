#ifndef SOUND_LM393_H
#define SOUND_LM393_H

/* 声音传感器（LM393 比较器模块，模拟输出），返回原始电压。 */
#include <stdint.h>
#include "bus/adc.h"

#ifdef __cplusplus
extern "C" {
#endif

int sound_init(const adc_bus_t *bus);
int sound_read(const adc_bus_t *bus, uint16_t *raw);

#ifdef __cplusplus
}
#endif

#endif /* SOUND_LM393_H */