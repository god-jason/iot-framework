#ifndef WS2812B_H
#define WS2812B_H

/*
 * WS2812B / SK6812 可编程 RGB 灯带驱动。
 *
 * 采用 SPI 编码方案：每个 LED 数据位用 3 个 SPI 位表示
 *   0 -> 0b100，1 -> 0b110
 * 因此 1 个 LED（24 bit）需要 9 字节 SPI 数据。
 *
 * 缓冲区由调用方提供，驱动不做动态分配：
 *   pixels  : 3 * led_count 字节（GRB）
 *   encoded : 9 * led_count 字节
 *
 * SPI 速率建议 2.4MHz（WS2812B 时序 0.4us/1.25us）。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    driver_spi_t spi;
    uint16_t led_count;
    uint8_t *pixels;
    uint8_t *encoded;
    bool initialized;
} ws2812b_handle_t;

int ws2812b_init(ws2812b_handle_t *handle, iot_spi_t *spi, iot_pin_t cs,
                 uint16_t led_count, uint8_t *pixels, uint8_t *encoded);
int ws2812b_deinit(ws2812b_handle_t *handle);

/** 设置单个像素（RGB 顺序，内部转为 GRB） */
int ws2812b_set_pixel(ws2812b_handle_t *handle, uint16_t index,
                      uint8_t red, uint8_t green, uint8_t blue);

/** 设置全部像素 */
int ws2812b_set_all(ws2812b_handle_t *handle, uint8_t red, uint8_t green, uint8_t blue);

/** 将 pixels 编码并通过 SPI 发送 */
int ws2812b_show(ws2812b_handle_t *handle);

/** 清空（不自动刷新） */
int ws2812b_clear(ws2812b_handle_t *handle);

/** 返回所需缓冲区字节数 */
uint32_t ws2812b_pixel_buffer_size(uint16_t led_count);
uint32_t ws2812b_encoded_buffer_size(uint16_t led_count);

#ifdef __cplusplus
}
#endif

#endif /* WS2812B_H */
