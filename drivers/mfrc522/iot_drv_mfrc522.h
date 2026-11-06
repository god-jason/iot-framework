#ifndef IOT_DRV_MFRC522_H
#define IOT_DRV_MFRC522_H

// MFRC522 13.56MHz RFID 读卡器驱动（SPI，寄存器地址最高位置 1 表示读）。
// 简化实现：寄存器读写、软复位与 REQA 请求，不含完整认证流程。
#include "bus/iot_bus_spi.h"

#ifdef __cplusplus
extern "C" {
#endif

int iot_mfrc522_init(const iot_spi_bus_t *spi);
int iot_mfrc522_request(const iot_spi_bus_t *spi, uint8_t *type);

#ifdef __cplusplus
}
#endif

#endif