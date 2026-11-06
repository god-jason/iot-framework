#ifndef IOT_SENSOR_SDS011_H
#define IOT_SENSOR_SDS011_H

/* SDS011 颗粒物传感器（UART 查询式，10 字节帧，数据为小端 16 位）。 */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 解析一帧查询应答，输出 PM2.5 / PM10（单位 ug/m³）。len 为帧长。 */
int iot_sds_parse(const uint8_t *frame, uint16_t len,
                  uint16_t *pm25, uint16_t *pm10);

#ifdef __cplusplus
}
#endif

#endif /* IOT_SENSOR_SDS011_H */