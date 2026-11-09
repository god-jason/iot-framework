#ifndef SSD1315_H
#define SSD1315_H

/*
 * SSD1315 128x64 单色 OLED 驱动，支持 I2C 与 4 线 SPI。
 * 命令集与 SSD1306 兼容，显存为页寻址格式 gram[page * 128 + x]。
 * 显存由调用方提供（>= SSD1315_GRAM_SIZE 字节），驱动不做动态分配。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SSD1315_BUS_I2C = 0,
    SSD1315_BUS_SPI
} ssd1315_bus_e;

typedef struct {
    ssd1315_bus_e bus_type;

    driver_i2c_t i2c;       /* I2C 模式 */
    driver_spi_t spi;       /* SPI 模式 */
    hal_pin_t dc;          /* 命令/数据选择，SPI 必填 */
    hal_pin_t res;         /* 复位，可为 0 */

    uint8_t *gram;
    size_t gram_len;
    bool mirror_x;
    bool mirror_y;
} ssd1315_config_t;

typedef struct {
    ssd1315_config_t config;
    uint8_t width;
    uint8_t height;
    bool initialized;
} ssd1315_handle_t;

int ssd1315_init(ssd1315_handle_t *handle, const ssd1315_config_t *config);
int ssd1315_deinit(ssd1315_handle_t *handle);

int ssd1315_write_cmd(ssd1315_handle_t *handle, const uint8_t *cmd, size_t len);
int ssd1315_write_data(ssd1315_handle_t *handle, const uint8_t *data, size_t len);

int ssd1315_clear(ssd1315_handle_t *handle);
int ssd1315_fill(ssd1315_handle_t *handle, uint8_t pattern);
int ssd1315_draw_pixel(ssd1315_handle_t *handle, uint8_t x, uint8_t y, bool on);
int ssd1315_show(ssd1315_handle_t *handle);

int ssd1315_set_contrast(ssd1315_handle_t *handle, uint8_t contrast);
int ssd1315_set_display(ssd1315_handle_t *handle, bool on);
int ssd1315_set_invert(ssd1315_handle_t *handle, bool invert);

#ifdef __cplusplus
}
#endif

#endif /* SSD1315_H */
