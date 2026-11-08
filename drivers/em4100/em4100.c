#include <string.h>
#include "em4100.h"
#include "em4100_register.h"
#include "log.h"

#define EM4100_CHECK(h) \
    do { if ((h) == NULL || !(h)->initialized) { return (int)HAL_INVALID_PARAM; } } while (0)

static int em4100_try_decode(const uint8_t *bits, em4100_tag_t *tag)
{
    uint8_t frame[EM4100_FRAME_BITS];
    uint8_t nibble[EM4100_ROWS];
    uint8_t col_parity[EM4100_COLS] = { 0U, 0U, 0U, 0U };
    size_t i;
    size_t row;
    size_t col;

    /* Manchester：1 -> (1,0)，0 -> (0,1) */
    for (i = 0U; i < EM4100_FRAME_BITS; i++) {
        uint8_t a = (bits[2U * i] != 0U) ? 1U : 0U;
        uint8_t b = (bits[2U * i + 1U] != 0U) ? 1U : 0U;
        if (a == b) { return (int)HAL_NOT_FOUND; }
        frame[i] = a;
    }
    for (i = 0U; i < EM4100_HEADER_BITS; i++) {
        if (frame[i] != 1U) { return (int)HAL_NOT_FOUND; }
    }
    for (row = 0U; row < EM4100_ROWS; row++) {
        uint8_t value = 0U;
        uint8_t sum = 0U;
        for (col = 0U; col < EM4100_COLS; col++) {
            uint8_t bit = frame[EM4100_HEADER_BITS + row * 5U + col];
            value = (uint8_t)((value << 1) | bit);
            sum = (uint8_t)(sum + bit);
            col_parity[col] = (uint8_t)(col_parity[col] + bit);
        }
        /* 偶校验：4 个数据位之和的最低位应等于校验位 */
        if ((uint8_t)(sum & 0x01U) != frame[EM4100_HEADER_BITS + row * 5U + 4U]) {
            return (int)HAL_CRC_ERROR;
        }
        nibble[row] = value;
    }
    for (col = 0U; col < EM4100_COLS; col++) {
        if ((uint8_t)(col_parity[col] & 0x01U) != frame[59U + col]) {
            return (int)HAL_CRC_ERROR;
        }
    }
    if (frame[63] != 0U) { return (int)HAL_CRC_ERROR; }

    tag->id = 0U;
    for (row = 0U; row < EM4100_ROWS; row++) {
        tag->id = (tag->id << 4) | (uint64_t)nibble[row];
    }
    for (i = 0U; i < EM4100_ID_BYTES; i++) {
        tag->raw[i] = (uint8_t)(tag->id >> ((EM4100_ID_BYTES - 1U - i) * 8U));
    }
    tag->valid = true;
    return DRIVER_OK;
}

int em4100_decode_manchester(const uint8_t *bits, size_t bit_count, em4100_tag_t *tag)
{
    size_t start;
    int ret;
    if (bits == NULL || tag == NULL) { return (int)HAL_INVALID_PARAM; }
    tag->valid = false;
    if (bit_count < EM4100_HALF_BITS) { return (int)HAL_INVALID_PARAM; }
    for (start = 0U; start + EM4100_HALF_BITS <= bit_count; start++) {
        ret = em4100_try_decode(&bits[start], tag);
        if (ret == DRIVER_OK) { return DRIVER_OK; }
    }
    return (int)HAL_NOT_FOUND;
}

int em4100_decode_frame(const uint8_t frame[5], em4100_tag_t *tag)
{
    uint64_t id = 0U;
    size_t i;
    if (frame == NULL || tag == NULL) { return (int)HAL_INVALID_PARAM; }
    for (i = 0U; i < EM4100_ID_BYTES; i++) {
        id = (id << 8) | (uint64_t)frame[i];
    }
    if (id == 0U) {
        tag->valid = false;
        return (int)HAL_INVALID_PARAM;
    }
    (void)memcpy(tag->raw, frame, EM4100_ID_BYTES);
    tag->id = id;
    tag->valid = true;
    return DRIVER_OK;
}

int em4100_set_timing(em4100_handle_t *h, uint32_t half_bit_us)
{
    EM4100_CHECK(h);
    if (half_bit_us < 50U || half_bit_us > 5000U) { return (int)HAL_INVALID_PARAM; }
    h->half_bit_us = half_bit_us;
    return DRIVER_OK;
}

int em4100_read(em4100_handle_t *h, em4100_tag_t *tag, uint32_t timeout_ms)
{
    uint8_t bits[EM4100_SAMPLES];
    uint32_t start;
    size_t i;
    int last;

    EM4100_CHECK(h);
    if (tag == NULL) { return (int)HAL_INVALID_PARAM; }
    tag->valid = false;
    last = driver_gpio_read(h->pin);
    if (last < 0) { return (int)HAL_IO_ERROR; }
    start = driver_tick_ms();
    /* 等待第一个跳变，作为采样起点 */
    for (;;) {
        int value = driver_gpio_read(h->pin);
        if (value < 0) { return (int)HAL_IO_ERROR; }
        if (value != last) { break; }
        if ((uint32_t)(driver_tick_ms() - start) > timeout_ms) {
            return (int)HAL_TIMEOUT;
        }
        driver_delay_us(EM4100_EDGE_POLL_US);
    }
    /* 以半位为间隔连续采样 */
    for (i = 0U; i < EM4100_SAMPLES; i++) {
        int value;
        driver_delay_us(h->half_bit_us);
        value = driver_gpio_read(h->pin);
        if (value < 0) { return (int)HAL_IO_ERROR; }
        bits[i] = (uint8_t)((value != 0) ? 1U : 0U);
    }
    /* 若首尾相位不理想，解码函数会自行搜索偏移 */
    return em4100_decode_manchester(bits, EM4100_SAMPLES, tag);
}

int em4100_init(em4100_handle_t *handle, hal_pin_t pin, uint32_t half_bit_us)
{
    if (handle == NULL || pin == 0U) { return (int)HAL_INVALID_PARAM; }
    if (half_bit_us < 50U || half_bit_us > 5000U) { return (int)HAL_INVALID_PARAM; }
    (void)memset(handle, 0, sizeof(*handle));
    handle->pin = pin;
    handle->half_bit_us = half_bit_us;
    handle->timeout_ms = 200U;
    if (driver_gpio_input(pin, HAL_GPIO_PULLUP) != DRIVER_OK) {
        LOG_E("em4100", "gpio init failed");
        return (int)HAL_IO_ERROR;
    }
    handle->initialized = true;
    return DRIVER_OK;
}

int em4100_deinit(em4100_handle_t *handle)
{
    if (handle == NULL) { return (int)HAL_INVALID_PARAM; }
    handle->initialized = false;
    return DRIVER_OK;
}
