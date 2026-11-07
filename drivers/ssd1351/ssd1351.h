#ifndef SSD1351_H
#define SSD1351_H

/*
 * SSD1351 128x128 RGB OLED 驱动（4 线 SPI，16 位 RGB565）。
 *
 * 显存由调用方提供（>= SSD1351_GRAM_SIZE = 32768 字节），RGB565 高字节在前，
 * row-major 排列。驱动不做动态分配。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    driver_spi_t spi;
    port_pin_t dc;          /* 命令/数据选择，必填 */
    port_pin_t res;         /* 复位，可为 0 */

    uint8_t *gram;
    size_t gram_len;

    bool mirror_x;
    bool mirror_y;
} ssd1351_config_t;

typedef struct {
    ssd1351_config_t config;
    uint8_t width;
    uint8_t height;
    bool initialized;
} ssd1351_handle_t;

int ssd1351_init(ssd1351_handle_t *handle, const ssd1351_config_t *config);
int ssd1351_deinit(ssd1351_handle_t *handle);

int ssd1351_write_cmd(ssd1351_handle_t *handle, const uint8_t *cmd, size_t len);
int ssd1351_write_data(ssd1351_handle_t *handle, const uint8_t *data, size_t len);

int ssd1351_clear(ssd1351_handle_t *handle);
int ssd1351_fill_color(ssd1351_handle_t *handle, uint16_t color);
int ssd1351_draw_pixel(ssd1351_handle_t *handle, uint8_t x, uint8_t y, uint16_t color);
int ssd1351_show(ssd1351_handle_t *handle);

int ssd1351_set_display(ssd1351_handle_t *handle, bool on);
int ssd1351_set_contrast(ssd1351_handle_t *handle, uint8_t contrast);

#ifdef __cplusplus
}
#endif

#endif /* SSD1351_H */
