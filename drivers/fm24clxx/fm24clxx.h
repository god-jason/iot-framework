#ifndef FM24CLXX_H
#define FM24CLXX_H

/*
 * FM24CLxx 系列 I2C FRAM 驱动（16 位地址）。
 *
 * FRAM 无写延时、无页边界限制；分块仅用于适配 I2C 传输长度上限。
 * 容量：FM24CL04=512B，CL08=1KB，CL16=2KB，CL32=4KB，CL64=8KB，
 * CL128=16KB，CL256=32KB，CL512=64KB。
 */

#include "common/driver_io.h"
#include "fm24clxx_register.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    driver_i2c_t i2c;
    uint32_t capacity;      /* 字节 */
    bool initialized;
} fm24clxx_handle_t;

int fm24clxx_init(fm24clxx_handle_t *handle, hal_i2c_t *i2c,
                  uint16_t address, uint32_t capacity);
int fm24clxx_deinit(fm24clxx_handle_t *handle);

int fm24clxx_read(fm24clxx_handle_t *handle, uint16_t address, uint8_t *data, size_t len);
int fm24clxx_write(fm24clxx_handle_t *handle, uint16_t address, const uint8_t *data,
                   size_t len);

int fm24clxx_get_capacity(fm24clxx_handle_t *handle, uint32_t *capacity);
/* 探测器件是否应答（FRAM 无 ID 寄存器） */
int fm24clxx_is_ready(fm24clxx_handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* FM24CLXX_H */
