#include <string.h>
#include "em4095.h"
#include "em4095_register.h"
#include "log.h"

#define EM4095_CHECK(h) \
    do { if ((h) == NULL || !(h)->initialized) { return (int)HAL_INVALID_PARAM; } } while (0)

static int em4095_wait_level(hal_pin_t pin, int level, uint32_t timeout_ms)
{
    uint32_t start = driver_tick_ms();
    for (;;) {
        int value = driver_gpio_read(pin);
        if (value < 0) { return (int)HAL_IO_ERROR; }
        if (value == level) { return DRIVER_OK; }
        if ((uint32_t)(driver_tick_ms() - start) > timeout_ms) { return (int)HAL_TIMEOUT; }
        driver_delay_us(EM4095_EDGE_POLL_US);
    }
}

int em4095_set_power(em4095_handle_t *h, bool on)
{
    EM4095_CHECK(h);
    return (driver_gpio_write(h->shd, on ? HAL_GPIO_LOW : HAL_GPIO_HIGH) == DRIVER_OK)
               ? DRIVER_OK : (int)HAL_IO_ERROR;
}

int em4095_set_modulation(em4095_handle_t *h, bool on)
{
    EM4095_CHECK(h);
    return (driver_gpio_write(h->mod, on ? HAL_GPIO_HIGH : HAL_GPIO_LOW) == DRIVER_OK)
               ? DRIVER_OK : (int)HAL_IO_ERROR;
}

int em4095_read(em4095_handle_t *h, uint8_t *bits, size_t max_bits,
                size_t *bit_count, uint32_t timeout_ms)
{
    size_t count = 0U;
    EM4095_CHECK(h);
    if (bits == NULL || max_bits == 0U) { return (int)HAL_INVALID_PARAM; }
    if (bit_count != NULL) { *bit_count = 0U; }

    /* 同步到第一个 RDY_CLK 上升沿 */
    if (em4095_wait_level(h->rdy_clk, 0, timeout_ms) != DRIVER_OK ||
        em4095_wait_level(h->rdy_clk, 1, timeout_ms) != DRIVER_OK) {
        return (int)HAL_TIMEOUT;
    }
    while (count < max_bits) {
        int value;
        driver_delay_us(EM4095_SAMPLE_DELAY_US);
        value = driver_gpio_read(h->dout);
        if (value < 0) { return (int)HAL_IO_ERROR; }
        bits[count++] = (uint8_t)((value != 0) ? 1U : 0U);
        if (em4095_wait_level(h->rdy_clk, 0, timeout_ms) != DRIVER_OK ||
            em4095_wait_level(h->rdy_clk, 1, timeout_ms) != DRIVER_OK) {
            break;   /* 卡离开或时钟停止 */
        }
    }
    if (bit_count != NULL) { *bit_count = count; }
    return (count > 0U) ? DRIVER_OK : (int)HAL_TIMEOUT;
}

int em4095_init(em4095_handle_t *handle, hal_pin_t shd, hal_pin_t mod,
                hal_pin_t rdy_clk, hal_pin_t dout)
{
    if (handle == NULL || shd == 0U || mod == 0U || rdy_clk == 0U || dout == 0U) {
        return (int)HAL_INVALID_PARAM;
    }
    (void)memset(handle, 0, sizeof(*handle));
    handle->shd = shd;
    handle->mod = mod;
    handle->rdy_clk = rdy_clk;
    handle->dout = dout;
    handle->timeout_ms = 500U;
    /* 初始为关断，避免上电即辐射载波 */
    if (driver_gpio_output(shd, HAL_GPIO_HIGH) != DRIVER_OK ||
        driver_gpio_output(mod, HAL_GPIO_LOW) != DRIVER_OK ||
        driver_gpio_input(rdy_clk, HAL_GPIO_NOPULL) != DRIVER_OK ||
        driver_gpio_input(dout, HAL_GPIO_NOPULL) != DRIVER_OK) {
        LOG_E("em4095", "gpio init failed");
        return (int)HAL_IO_ERROR;
    }
    handle->initialized = true;
    return DRIVER_OK;
}

int em4095_deinit(em4095_handle_t *handle)
{
    if (handle == NULL) { return (int)HAL_INVALID_PARAM; }
    if (handle->initialized) {
        (void)driver_gpio_write(handle->shd, HAL_GPIO_HIGH);
        (void)driver_gpio_write(handle->mod, HAL_GPIO_LOW);
    }
    handle->initialized = false;
    return DRIVER_OK;
}
