#ifndef W25QXX_H
#define W25QXX_H

/*
 * W25Q 系列 SPI NOR Flash 驱动（W25Q16/32/64/128/256 等）。
 *
 * 参考 libdriver 风格：w25qxx_init / w25qxx_read / w25qxx_write /
 * w25qxx_erase_sector。写操作前需保证目标区域已擦除。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t manufacturer;
    uint8_t memory_type;
    uint8_t capacity_id;
} w25qxx_id_t;

typedef struct {
    driver_spi_t spi;
    w25qxx_id_t id;
    uint32_t capacity;      /* 字节 */
    bool initialized;
} w25qxx_handle_t;

int w25qxx_init(w25qxx_handle_t *handle, iot_spi_t *spi, iot_pin_t cs);
int w25qxx_deinit(w25qxx_handle_t *handle);

int w25qxx_read_id(w25qxx_handle_t *handle, w25qxx_id_t *id);

int w25qxx_read(w25qxx_handle_t *handle, uint32_t address, void *data, size_t len);
int w25qxx_write(w25qxx_handle_t *handle, uint32_t address, const void *data, size_t len);
int w25qxx_erase_sector(w25qxx_handle_t *handle, uint32_t address);
int w25qxx_erase_block(w25qxx_handle_t *handle, uint32_t address);
int w25qxx_erase_chip(w25qxx_handle_t *handle);

/** 等待 BUSY 清零 */
int w25qxx_wait_busy(w25qxx_handle_t *handle, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* W25QXX_H */
