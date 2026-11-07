#ifndef MAX6675_H
#define MAX6675_H

/* MAX6675 K 型热电偶数字转换器（SPI，0.25℃ 分辨率）。 */
#include "bus/spi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化（芯片无片上配置，仅校验总线） */
int max6675_init(const spi_bus_t *spi);

/* 读取温度(℃)；热电偶开路时返回负错误码 */
int max6675_read(const spi_bus_t *spi, float *temp);

#ifdef __cplusplus
}
#endif

#endif /* MAX6675_H */