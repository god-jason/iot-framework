#ifndef ST7920_H
#define ST7920_H

/*
 * ST7920 128x64 点阵 LCD 驱动（串行 3 线 SPI 模式，仅 MOSI/SCLK/CS）。
 *
 * 显存由调用方提供（>= ST7920_GRAM_SIZE = 1024 字节），布局为
 * gram[y * 16 + x / 8]，每字节 8 个水平像素，LSB 为最左像素。
 * 驱动不做动态分配。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    driver_spi_t spi;
    iot_pin_t res;         /* 复位，可为 0（模块无复位脚时） */

    uint8_t *gram;
    size_t gram_len;

    bool mirror_x;
    bool mirror_y;
} st7920_config_t;

typedef struct {
    st7920_config_t config;
    uint8_t width;
    uint8_t height;
    bool initialized;
} st7920_handle_t;

int st7920_init(st7920_handle_t *handle, const st7920_config_t *config);
int st7920_deinit(st7920_handle_t *handle);

int st7920_write_cmd(st7920_handle_t *handle, uint8_t cmd);
int st7920_write_data(st7920_handle_t *handle, uint8_t data);

int st7920_clear(st7920_handle_t *handle);
int st7920_fill(st7920_handle_t *handle, uint8_t pattern);
int st7920_draw_pixel(st7920_handle_t *handle, uint8_t x, uint8_t y, bool on);
int st7920_show(st7920_handle_t *handle);

int st7920_set_display(st7920_handle_t *handle, bool on);

#ifdef __cplusplus
}
#endif

#endif /* ST7920_H */
