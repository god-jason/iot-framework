#include "ws2812b.h"
#include "log.h"

#define WS2812B_BYTES_PER_LED       3U
#define WS2812B_SPI_BYTES_PER_LED   9U
#define WS2812B_RESET_DELAY_US      60U

uint32_t ws2812b_pixel_buffer_size(uint16_t led_count)
{
    return (uint32_t)led_count * WS2812B_BYTES_PER_LED;
}

uint32_t ws2812b_encoded_buffer_size(uint16_t led_count)
{
    return (uint32_t)led_count * WS2812B_SPI_BYTES_PER_LED;
}

/* 将一个字节展开为 3 字节 SPI 码流 */
static void ws2812b_encode_byte(uint8_t value, uint8_t *out)
{
    uint32_t bits = 0U;
    uint8_t i;

    for (i = 0U; i < 8U; i++) {
        bits <<= 3;
        if ((value & 0x80U) != 0U) {
            bits |= 0x6U;   /* 110 */
        } else {
            bits |= 0x4U;   /* 100 */
        }
        value = (uint8_t)(value << 1);
    }

    out[0] = (uint8_t)((bits >> 16) & 0xFFU);
    out[1] = (uint8_t)((bits >> 8) & 0xFFU);
    out[2] = (uint8_t)(bits & 0xFFU);
}

int ws2812b_init(ws2812b_handle_t *handle, iot_spi_t *spi, iot_pin_t cs,
                 uint16_t led_count, uint8_t *pixels, uint8_t *encoded)
{
    if (handle == NULL || spi == NULL || led_count == 0U ||
        pixels == NULL || encoded == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    handle->spi.handle = spi;
    handle->spi.cs = cs;
    handle->spi.timeout_ms = DRIVER_SPI_TIMEOUT_DEFAULT;
    handle->led_count = led_count;
    handle->pixels = pixels;
    handle->encoded = encoded;
    handle->initialized = false;

    if (cs != 0U) {
        if (driver_gpio_output(cs, IOT_GPIO_HIGH) != DRIVER_OK) {
            return (int)IOT_IO_ERROR;
        }
    }

    /* 建议 2.4MHz；平台不支持时保持原速率 */
    (void)iot_spi_set_speed(spi, 2400000U);

    (void)ws2812b_clear(handle);
    handle->initialized = true;
    LOG_I("ws2812b", "init ok, leds=%u", (unsigned)led_count);
    return DRIVER_OK;
}

int ws2812b_deinit(ws2812b_handle_t *handle)
{
    if (handle == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    handle->initialized = false;
    handle->pixels = NULL;
    handle->encoded = NULL;
    return DRIVER_OK;
}

int ws2812b_set_pixel(ws2812b_handle_t *handle, uint16_t index,
                      uint8_t red, uint8_t green, uint8_t blue)
{
    uint32_t offset;

    if (handle == NULL || index >= handle->led_count || handle->pixels == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    offset = (uint32_t)index * WS2812B_BYTES_PER_LED;
    handle->pixels[offset + 0U] = green;   /* WS2812B 数据顺序为 GRB */
    handle->pixels[offset + 1U] = red;
    handle->pixels[offset + 2U] = blue;
    return DRIVER_OK;
}

int ws2812b_set_all(ws2812b_handle_t *handle, uint8_t red, uint8_t green, uint8_t blue)
{
    uint16_t i;
    int ret;

    if (handle == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    for (i = 0U; i < handle->led_count; i++) {
        ret = ws2812b_set_pixel(handle, i, red, green, blue);
        if (ret != DRIVER_OK) {
            return ret;
        }
    }
    return DRIVER_OK;
}

int ws2812b_clear(ws2812b_handle_t *handle)
{
    if (handle == NULL || handle->pixels == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    for (uint32_t i = 0U; i < ws2812b_pixel_buffer_size(handle->led_count); i++) {
        handle->pixels[i] = 0U;
    }
    return DRIVER_OK;
}

int ws2812b_show(ws2812b_handle_t *handle)
{
    uint16_t led;
    int ret;

    if (handle == NULL || !handle->initialized ||
        handle->pixels == NULL || handle->encoded == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    for (led = 0U; led < handle->led_count; led++) {
        uint32_t src = (uint32_t)led * WS2812B_BYTES_PER_LED;
        uint32_t dst = (uint32_t)led * WS2812B_SPI_BYTES_PER_LED;

        ws2812b_encode_byte(handle->pixels[src + 0U], &handle->encoded[dst + 0U]);
        ws2812b_encode_byte(handle->pixels[src + 1U], &handle->encoded[dst + 3U]);
        ws2812b_encode_byte(handle->pixels[src + 2U], &handle->encoded[dst + 6U]);
    }

    ret = driver_spi_write_cs(&handle->spi, handle->encoded,
                              ws2812b_encoded_buffer_size(handle->led_count));
    if (ret != DRIVER_OK) {
        return ret;
    }

    /* 锁存复位：拉低 >50us */
    driver_delay_us(WS2812B_RESET_DELAY_US);
    return DRIVER_OK;
}
