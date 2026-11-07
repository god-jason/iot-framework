#ifndef PMS5003_H
#define PMS5003_H

/* PMS5003 颗粒物传感器（UART 主动上报，32 字节帧，大端 16 位）。 */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 解析一帧主动上报数据，输出 PM1.0 / PM2.5 / PM10（单位 ug/m³）。len 为帧长。 */
int pms_parse(const uint8_t *frame, uint16_t len,
                  uint16_t *pm1, uint16_t *pm25, uint16_t *pm10);

#ifdef __cplusplus
}
#endif

#endif /* PMS5003_H */