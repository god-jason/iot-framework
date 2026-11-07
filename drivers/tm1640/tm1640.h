#ifndef TM1640_H
#define TM1640_H

/*
 * TM1640 16 段 LED 驱动（2 线 GPIO：SCLK / DIN，软件时序）。
 *
 * 显存由调用方提供（>= TM1640_GRID_COUNT = 16 字节），每字节对应一个 grid，
 * bit0..bit7 对应段 a..dp。
 * draw_pixel 语义：x = grid 序号（0..15），y = 段位（0..7）。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    port_pin_t sclk;
    port_pin_t din;

    uint8_t *gram;
    size_t gram_len;
    uint8_t brightness;     /* 0..7 */
} tm1640_config_t;

typedef struct {
    tm1640_config_t config;
    bool initialized;
} tm1640_handle_t;

int tm1640_init(tm1640_handle_t *handle, const tm1640_config_t *config);
int tm1640_deinit(tm1640_handle_t *handle);

int tm1640_clear(tm1640_handle_t *handle);
int tm1640_fill(tm1640_handle_t *handle, uint8_t pattern);
int tm1640_show(tm1640_handle_t *handle);
int tm1640_draw_pixel(tm1640_handle_t *handle, uint8_t x, uint8_t y, bool on);

int tm1640_set_brightness(tm1640_handle_t *handle, uint8_t brightness);
int tm1640_set_display(tm1640_handle_t *handle, bool on);

#ifdef __cplusplus
}
#endif

#endif /* TM1640_H */
