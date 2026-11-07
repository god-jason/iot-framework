#ifndef TM1637_H
#define TM1637_H

/*
 * TM1637 7 段数码管驱动（2 线 GPIO：CLK / DIO，软件时序）。
 *
 * 显存由调用方提供：每个数码管一个字节段码，长度 >= digits。
 * draw_pixel 语义：x = 数码管序号（0..digits-1），y = 段位（0..7，a..dp）。
 * 段码写入显存后需调用 tm1637_show 才会显示。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    iot_pin_t clk;
    iot_pin_t dio;

    uint8_t *gram;          /* 每位数码管一个段码字节 */
    size_t gram_len;
    uint8_t digits;         /* 4 或 6 */
    uint8_t brightness;     /* 0..7 */
} tm1637_config_t;

typedef struct {
    tm1637_config_t config;
    bool initialized;
} tm1637_handle_t;

int tm1637_init(tm1637_handle_t *handle, const tm1637_config_t *config);
int tm1637_deinit(tm1637_handle_t *handle);

int tm1637_clear(tm1637_handle_t *handle);
int tm1637_show(tm1637_handle_t *handle);

/** x: 数码管序号, y: 段位(0..7), on: 点亮/熄灭 */
int tm1637_draw_pixel(tm1637_handle_t *handle, uint8_t x, uint8_t y, bool on);

int tm1637_set_brightness(tm1637_handle_t *handle, uint8_t brightness);
int tm1637_set_display(tm1637_handle_t *handle, bool on);

#ifdef __cplusplus
}
#endif

#endif /* TM1637_H */
