#ifndef NTAG21X_H
#define NTAG21X_H

/*
 * NTAG21x (NTAG213/215/216) NFC 标签存储区驱动（I2C 接口）。
 *
 * 说明：NTAG21x 本身是 ISO14443A 标签，这里面向带 I2C 前端（如 NTAG I2C
 * 系列或标签仿真芯片）的访问方式：以页号为寄存器地址，每页 4 字节。
 */

#include "common/driver_io.h"
#include "ntag21x_register.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    driver_i2c_t i2c;
    uint16_t page_count;    /* 默认 231（NTAG216 用户区） */
    uint8_t page_size;
    bool initialized;
} ntag21x_handle_t;

int ntag21x_init(ntag21x_handle_t *handle, port_i2c_handle_t *i2c, uint16_t address);
int ntag21x_deinit(ntag21x_handle_t *handle);

/* 页对齐读写，len 可为任意值；写操作要求 len 为 page_size 的整数倍 */
int ntag21x_read(ntag21x_handle_t *handle, uint16_t page, uint8_t *data, size_t len);
int ntag21x_write(ntag21x_handle_t *handle, uint16_t page, const uint8_t *data, size_t len);

int ntag21x_get_page_count(ntag21x_handle_t *handle, uint16_t *pages);
/* 按型号设置用户区页数：NTAG213=45，NTAG215=135，NTAG216=231 */
int ntag21x_set_page_count(ntag21x_handle_t *handle, uint16_t pages);

#ifdef __cplusplus
}
#endif

#endif /* NTAG21X_H */
