#ifndef SSD1306_H
#define SSD1306_H

/*
 * SSD1306 128x64 单色 OLED 驱动，支持 I2C 与 4 线 SPI。
 *
 * 显存由调用方提供（SSD1306_GRAM_SIZE 字节），驱动不做动态分配。
 * 参考 libdriver 风格：ssd1306_init / ssd1306_deinit / ssd1306_write_cmd /
 * ssd1306_write_data / ssd1306_clear / ssd1306_show。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SSD1306_BUS_I2C = 0,
    SSD1306_BUS_SPI
} ssd1306_bus_t;

typedef struct {
    ssd1306_bus_t bus_type;

    /* I2C 模式 */
    driver_i2c_t i2c;

    /* SPI 模式 */
    driver_spi_t spi;
    iot_pin_t dc;      /* 数据/命令选择 */
    iot_pin_t res;     /* 复位，可为 0 */

    uint8_t *gram;      /* 显存，长度必须 >= SSD1306_GRAM_SIZE */
    bool mirror_x;
    bool mirror_y;
} ssd1306_config_t;

typedef struct {
    ssd1306_config_t config;
    uint8_t width;
    uint8_t height;
    bool initialized;
} ssd1306_handle_t;

int ssd1306_init(ssd1306_handle_t *handle, const ssd1306_config_t *config);
int ssd1306_deinit(ssd1306_handle_t *handle);

int ssd1306_write_cmd(ssd1306_handle_t *handle, const uint8_t *cmd, size_t len);
int ssd1306_write_data(ssd1306_handle_t *handle, const uint8_t *data, size_t len);

int ssd1306_clear(ssd1306_handle_t *handle);
int ssd1306_fill(ssd1306_handle_t *handle, uint8_t pattern);
int ssd1306_draw_pixel(ssd1306_handle_t *handle, uint8_t x, uint8_t y, bool on);

/** 将显存刷新到屏幕 */
int ssd1306_show(ssd1306_handle_t *handle);

int ssd1306_set_contrast(ssd1306_handle_t *handle, uint8_t contrast);
int ssd1306_set_display(ssd1306_handle_t *handle, bool on);
int ssd1306_set_invert(ssd1306_handle_t *handle, bool invert);

#ifdef __cplusplus
}
#endif

#endif /* SSD1306_H */
