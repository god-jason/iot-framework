#ifndef SSD1681_H
#define SSD1681_H

/*
 * SSD1681 200x200 电子墨水屏驱动（4 线 SPI，1bpp，带 BUSY 握手）。
 *
 * 显存由调用方提供（>= SSD1681_GRAM_SIZE = 5000 字节）：
 * gram[y * 25 + x / 8]，每字节 8 个水平像素，MSB 为最左像素。
 * draw_pixel 只修改显存，需调用 <chip>_show 才会真正刷新屏幕（墨水屏刷新较慢）。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    driver_spi_t spi;
    hal_pin_t dc;          /* 命令/数据选择，必填 */
    hal_pin_t res;         /* 复位，必填 */
    hal_pin_t busy;        /* BUSY 输入，高电平表示忙，必填 */

    uint8_t *gram;
    size_t gram_len;

    bool mirror_x;
    bool mirror_y;
} ssd1681_config_t;

typedef struct {
    ssd1681_config_t config;
    uint16_t width;
    uint16_t height;
    bool initialized;
    bool dirty;             /* 显存已修改，尚未刷新 */
} ssd1681_handle_t;

int ssd1681_init(ssd1681_handle_t *handle, const ssd1681_config_t *config);
int ssd1681_deinit(ssd1681_handle_t *handle);

int ssd1681_write_cmd(ssd1681_handle_t *handle, const uint8_t *cmd, size_t len);
int ssd1681_write_data(ssd1681_handle_t *handle, const uint8_t *data, size_t len);

int ssd1681_clear(ssd1681_handle_t *handle);
int ssd1681_fill(ssd1681_handle_t *handle, bool white);
int ssd1681_draw_pixel(ssd1681_handle_t *handle, uint16_t x, uint16_t y, bool black);

/** 写显存到 RAM 并触发一次全屏刷新（会等待 BUSY 释放） */
int ssd1681_show(ssd1681_handle_t *handle);

/** 读取 BUSY 引脚，1 表示忙 */
int ssd1681_is_busy(ssd1681_handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* SSD1681_H */
