#ifndef IOT_DRV_MAX31865_H
#define IOT_DRV_MAX31865_H

/* MAX31865 RTD 铂电阻温度转换器（SPI，PT100/PT1000）。 */
#include "bus/iot_bus_spi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化：写配置寄存器，使能偏置电压(VBIAS) */
int iot_max31865_init(const iot_spi_bus_t *spi);

/* 读取温度(℃)，采用线性近似：temp≈raw/32-256 */
int iot_max31865_read(const iot_spi_bus_t *spi, float *temp);

#ifdef __cplusplus
}
#endif

#endif /* IOT_DRV_MAX31865_H */