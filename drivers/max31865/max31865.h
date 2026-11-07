#ifndef MAX31865_H
#define MAX31865_H

/* MAX31865 RTD 铂电阻温度转换器（SPI，PT100/PT1000）。 */
#include "bus/spi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化：写配置寄存器，使能偏置电压(VBIAS) */
int max31865_init(const spi_bus_t *spi);

/* 读取温度(℃)，采用线性近似：temp≈raw/32-256 */
int max31865_read(const spi_bus_t *spi, float *temp);

#ifdef __cplusplus
}
#endif

#endif /* MAX31865_H */