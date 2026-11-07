#ifndef TM1638_H
#define TM1638_H

/*
 * TM1638 三线（STB / CLK / DIO，软件时序）LED 数码管与按键驱动。
 *
 * 显存由调用方提供（>= TM1638_REG_COUNT = 16 字节）：
 *   gram[0..7]  数码管段码（bit0=a ... bit7=dp）
 *   gram[8..15] LED，仅 bit0 有效
 * draw_pixel 语义：x = 寄存器序号（0..15），y = 位序号（0..7）。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    port_pin_t stb;
    port_pin_t clk;
    port_pin_t dio;

    uint8_t *gram;
    size_t gram_len;
    uint8_t brightness;     /* 0..7 */
} tm1638_config_t;

typedef struct {
    tm1638_config_t config;
    bool initialized;
} tm1638_handle_t;

int tm1638_init(tm1638_handle_t *handle, const tm1638_config_t *config);
int tm1638_deinit(tm1638_handle_t *handle);

int tm1638_clear(tm1638_handle_t *handle);
int tm1638_show(tm1638_handle_t *handle);

int tm1638_draw_pixel(tm1638_handle_t *handle, uint8_t x, uint8_t y, bool on);

/** 直接写 16 个显示/ LED 寄存器 */
int tm1638_write_regs(tm1638_handle_t *handle, const uint8_t *regs, size_t len);

/** 读 4 字节按键状态（每字节对应一个按键组，bit0..1 有效） */
int tm1638_read_keys(tm1638_handle_t *handle, uint8_t keys[TM1638_KEY_BYTES]);

int tm1638_set_brightness(tm1638_handle_t *handle, uint8_t brightness);
int tm1638_set_display(tm1638_handle_t *handle, bool on);

#ifdef __cplusplus
}
#endif

#endif /* TM1638_H */
