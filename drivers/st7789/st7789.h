#ifndef ST7789_H
#define ST7789_H

/*
 * ST7789 240x320 TFT 驱动（4 线 SPI，16 位 RGB565）。
 *
 * 显存由调用方提供，长度 >= width * height * 2，格式为 RGB565 高字节在前，
 * 与屏幕扫描顺序一致（row-major）。驱动不做动态分配。
 * 支持面板偏移（x_offset / y_offset），便于 135x240、240x240 等小尺寸面板复用。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    driver_spi_t spi;
    iot_pin_t dc;          /* 命令/数据选择，必填 */
    iot_pin_t res;         /* 复位，可为 0 */
    iot_pin_t bl;          /* 背光，可为 0 */

    uint8_t *gram;          /* RGB565 显存 */
    size_t gram_len;

    uint16_t width;         /* 0 表示使用 ST7789_WIDTH */
    uint16_t height;        /* 0 表示使用 ST7789_HEIGHT */
    uint16_t x_offset;      /* 面板列偏移 */
    uint16_t y_offset;      /* 面板行偏移 */

    bool mirror_x;
    bool mirror_y;
    bool bgr;               /* 面板为 BGR 顺序时置 true */
    bool invert;            /* 面板需要反显时置 true */
} st7789_config_t;

typedef struct {
    st7789_config_t config;
    uint16_t width;
    uint16_t height;
    bool initialized;
} st7789_handle_t;

int st7789_init(st7789_handle_t *handle, const st7789_config_t *config);
int st7789_deinit(st7789_handle_t *handle);

int st7789_write_cmd(st7789_handle_t *handle, const uint8_t *cmd, size_t len);
int st7789_write_data(st7789_handle_t *handle, const uint8_t *data, size_t len);

int st7789_clear(st7789_handle_t *handle);
int st7789_fill_color(st7789_handle_t *handle, uint16_t color);
int st7789_draw_pixel(st7789_handle_t *handle, uint16_t x, uint16_t y, uint16_t color);

/** 将显存刷新到屏幕 */
int st7789_show(st7789_handle_t *handle);

int st7789_set_backlight(st7789_handle_t *handle, bool on);

#ifdef __cplusplus
}
#endif

#endif /* ST7789_H */
