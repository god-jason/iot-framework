#ifndef MAX7219_H
#define MAX7219_H

/*
 * MAX7219 / MAX7221 LED 数码管与点阵驱动（SPI）。
 *
 * 支持级联多片：每帧 16 位，写操作会依次向所有片发送，
 * 目标片使用 NO-OP 填充。
 *
 * 参考 libdriver 风格：max7219_init / max7219_write / max7219_clear。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    driver_spi_t spi;
    uint8_t chain_length;   /* 级联片数，至少 1 */
    bool initialized;
} max7219_handle_t;

int max7219_init(max7219_handle_t *handle, port_spi_handle_t *spi, port_pin_t cs,
                 uint8_t chain_length);
int max7219_deinit(max7219_handle_t *handle);

/** 写单个寄存器（对所有级联片） */
int max7219_write(max7219_handle_t *handle, uint8_t reg, uint8_t data);

/** 指定片写寄存器（index 0 为最靠近 MCU 的一片） */
int max7219_write_index(max7219_handle_t *handle, uint8_t index,
                        uint8_t reg, uint8_t data);

int max7219_set_intensity(max7219_handle_t *handle, uint8_t intensity);
int max7219_set_decode_mode(max7219_handle_t *handle, uint8_t mode);
int max7219_set_scan_limit(max7219_handle_t *handle, uint8_t limit);
int max7219_set_shutdown(max7219_handle_t *handle, bool shutdown);
int max7219_display_test(max7219_handle_t *handle, bool enable);
int max7219_clear(max7219_handle_t *handle);
int max7219_set_digit(max7219_handle_t *handle, uint8_t digit, uint8_t value);

#ifdef __cplusplus
}
#endif

#endif /* MAX7219_H */
