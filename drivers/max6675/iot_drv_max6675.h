#ifndef IOT_DRV_MAX6675_H
#define IOT_DRV_MAX6675_H

/* MAX6675 K 型热电偶数字转换器（SPI，0.25℃ 分辨率）。 */
#include "bus/iot_bus_spi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化（芯片无片上配置，仅校验总线） */
int iot_max6675_init(const iot_spi_bus_t *spi);

/* 读取温度(℃)；热电偶开路时返回负错误码 */
int iot_max6675_read(const iot_spi_bus_t *spi, float *temp);

#ifdef __cplusplus
}
#endif

#endif /* IOT_DRV_MAX6675_H */