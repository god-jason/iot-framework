#ifndef IOT_SENSOR_SOUND_LM393_H
#define IOT_SENSOR_SOUND_LM393_H

/* 声音传感器（LM393 比较器模块，模拟输出），返回原始电压。 */
#include <stdint.h>
#include "bus/iot_bus_adc.h"

#ifdef __cplusplus
extern "C" {
#endif

int iot_sound_init(const iot_adc_bus_t *bus);
int iot_sound_read(const iot_adc_bus_t *bus, uint16_t *raw);

#ifdef __cplusplus
}
#endif

#endif /* IOT_SENSOR_SOUND_LM393_H */