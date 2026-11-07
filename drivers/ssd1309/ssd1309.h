#ifndef SSD1309_H
#define SSD1309_H

/*
 * SSD1309 128x64 单色 OLED 驱动，支持 I2C 与 4 线 SPI。
 *
 * 显存由调用方提供（SSD1309_GRAM_SIZE 字节，页寻址格式：
 * gram[page * 128 + x]，每字节表示 8 个垂直像素，LSB 在上），驱动不做动态分配。
 * 参考 libdriver 风格：ssd1309_init / ssd1309_deinit / ssd1309_write_cmd /
 * ssd1309_write_data / ssd1309_clear / ssd1309_show。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SSD1309_BUS_I2C = 0,
    SSD1309_BUS_SPI
} ssd1309_bus_t;

typedef struct {
    ssd1309_bus_t bus_type;

    /* I2C 模式 */
    driver_i2c_t i2c;

    /* SPI 模式 */
    driver_spi_t spi;
    iot_pin_t dc;      /* 数据/命令选择，SPI 模式必填 */
    iot_pin_t res;     /* 复位，可为 0（不使用） */

    uint8_t *gram;      /* 显存，长度必须 >= SSD1309_GRAM_SIZE */
    size_t gram_len;    /* 显存字节数，用于长度校验 */
    bool mirror_x;
    bool mirror_y;
} ssd1309_config_t;

typedef struct {
    ssd1309_config_t config;
    uint8_t width;
    uint8_t height;
    bool initialized;
} ssd1309_handle_t;

int ssd1309_init(ssd1309_handle_t *handle, const ssd1309_config_t *config);
int ssd1309_deinit(ssd1309_handle_t *handle);

int ssd1309_write_cmd(ssd1309_handle_t *handle, const uint8_t *cmd, size_t len);
int ssd1309_write_data(ssd1309_handle_t *handle, const uint8_t *data, size_t len);

int ssd1309_clear(ssd1309_handle_t *handle);
int ssd1309_fill(ssd1309_handle_t *handle, uint8_t pattern);
int ssd1309_draw_pixel(ssd1309_handle_t *handle, uint8_t x, uint8_t y, bool on);

/** 将显存刷新到屏幕 */
int ssd1309_show(ssd1309_handle_t *handle);

int ssd1309_set_contrast(ssd1309_handle_t *handle, uint8_t contrast);
int ssd1309_set_display(ssd1309_handle_t *handle, bool on);
int ssd1309_set_invert(ssd1309_handle_t *handle, bool invert);

#ifdef __cplusplus
}
#endif

#endif /* SSD1309_H */
