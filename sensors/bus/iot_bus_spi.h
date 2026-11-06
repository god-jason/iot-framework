#ifndef IOT_BUS_SPI_H
#define IOT_BUS_SPI_H

/*
 * 通用 SPI 总线抽象：所有 SPI 外设统一用本接口收发字节并控制片选。
 * 寄存器读写的地址位编码由各设备驱动自行处理。
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int (*transfer)(void *ctx, const uint8_t *tx, uint8_t *rx, uint32_t len);
    void (*cs_select)(void *ctx, bool select);
    void *ctx;
} iot_spi_bus_t;

/* 传输并回读（tx 可为 NULL 表示仅读，rx 可为 NULL 表示仅写） */
int iot_spi_transfer(const iot_spi_bus_t *bus, const uint8_t *tx, uint8_t *rx, uint32_t len);

/* 拉低/释放片选 */
void iot_spi_cs(const iot_spi_bus_t *bus, bool select);

#ifdef __cplusplus
}
#endif

#endif /* IOT_BUS_SPI_H */