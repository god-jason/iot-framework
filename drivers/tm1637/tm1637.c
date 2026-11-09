#include "tm1637.h"
#include "tm1637_register.h"
#include "log.h"

static void tm1637_delay(void)
{
    driver_delay_us(TM1637_DELAY_US);
}

static int tm1637_dio(tm1637_handle_t *handle, hal_gpio_level_e level)
{
    return driver_gpio_write(handle->config.dio, level);
}

static int tm1637_clk(tm1637_handle_t *handle, hal_gpio_level_e level)
{
    return driver_gpio_write(handle->config.clk, level);
}

/* 起始信号：CLK 高电平期间 DIO 由高变低 */
static int tm1637_start(tm1637_handle_t *handle)
{
    if (tm1637_dio(handle, HAL_GPIO_HIGH) != DRIVER_OK ||
        tm1637_clk(handle, HAL_GPIO_HIGH) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    tm1637_delay();
    if (tm1637_dio(handle, HAL_GPIO_LOW) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    tm1637_delay();
    return DRIVER_OK;
}

/* 停止信号：CLK 高电平期间 DIO 由低变高 */
static int tm1637_stop(tm1637_handle_t *handle)
{
    if (tm1637_clk(handle, HAL_GPIO_LOW) != DRIVER_OK ||
        tm1637_dio(handle, HAL_GPIO_LOW) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    tm1637_delay();
    if (tm1637_clk(handle, HAL_GPIO_HIGH) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    tm1637_delay();
    if (tm1637_dio(handle, HAL_GPIO_HIGH) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    tm1637_delay();
    return DRIVER_OK;
}

/* 写一个字节，低位在前；随后补一个 ACK 时钟（本层不读 ACK 电平） */
static int tm1637_write_byte(tm1637_handle_t *handle, uint8_t value)
{
    uint8_t i;

    for (i = 0U; i < 8U; i++) {
        if (tm1637_clk(handle, HAL_GPIO_LOW) != DRIVER_OK) {
            return (int)HAL_IO_ERROR;
        }
        tm1637_delay();
        if (tm1637_dio(handle, ((value >> i) & 0x01U) != 0U ? HAL_GPIO_HIGH : HAL_GPIO_LOW) != DRIVER_OK) {
            return (int)HAL_IO_ERROR;
        }
        tm1637_delay();
        if (tm1637_clk(handle, HAL_GPIO_HIGH) != DRIVER_OK) {
            return (int)HAL_IO_ERROR;
        }
        tm1637_delay();
    }

    /* 第 9 个时钟：释放 DIO，忽略从机应答 */
    if (tm1637_clk(handle, HAL_GPIO_LOW) != DRIVER_OK ||
        tm1637_dio(handle, HAL_GPIO_HIGH) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    tm1637_delay();
    if (tm1637_clk(handle, HAL_GPIO_HIGH) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    tm1637_delay();
    return tm1637_clk(handle, HAL_GPIO_LOW);
}

static int tm1637_update_display(tm1637_handle_t *handle, bool on)
{
    uint8_t cmd = on
                  ? (uint8_t)(TM1637_CMD_DISPLAY_ON | (handle->config.brightness & TM1637_BRIGHTNESS_MAX))
                  : (uint8_t)TM1637_CMD_DISPLAY_OFF;

    if (tm1637_start(handle) != DRIVER_OK ||
        tm1637_write_byte(handle, cmd) != DRIVER_OK ||
        tm1637_stop(handle) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    return DRIVER_OK;
}

int tm1637_init(tm1637_handle_t *handle, const tm1637_config_t *config)
{
    uint8_t i;

    if (handle == NULL || config == NULL || config->gram == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    if (config->clk == 0U || config->dio == 0U) {
        return (int)HAL_INVALID_PARAM;
    }
    if (config->digits == 0U || config->digits > TM1637_DIGITS_MAX ||
        config->gram_len < config->digits) {
        return (int)HAL_INVALID_PARAM;
    }
    if (config->brightness > TM1637_BRIGHTNESS_MAX) {
        return (int)HAL_INVALID_PARAM;
    }

    handle->config = *config;
    handle->initialized = false;

    if (driver_gpio_output(config->clk, HAL_GPIO_HIGH) != DRIVER_OK ||
        driver_gpio_output(config->dio, HAL_GPIO_HIGH) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }

    for (i = 0U; i < config->digits; i++) {
        handle->config.gram[i] = 0x00U;
    }

    /* 初始化：清显示 + 打开显示 */
    if (tm1637_start(handle) != DRIVER_OK ||
        tm1637_write_byte(handle, TM1637_CMD_DATA_AUTO) != DRIVER_OK ||
        tm1637_stop(handle) != DRIVER_OK) {
        LOG_E("tm1637", "init sequence failed");
        return (int)HAL_IO_ERROR;
    }
    if (tm1637_start(handle) != DRIVER_OK ||
        tm1637_write_byte(handle, TM1637_CMD_ADDR_BASE) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    for (i = 0U; i < config->digits; i++) {
        if (tm1637_write_byte(handle, 0x00U) != DRIVER_OK) {
            return (int)HAL_IO_ERROR;
        }
    }
    if (tm1637_stop(handle) != DRIVER_OK ||
        tm1637_update_display(handle, true) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }

    handle->initialized = true;
    LOG_I("tm1637", "init ok, digits=%u", (unsigned)config->digits);
    return DRIVER_OK;
}

int tm1637_deinit(tm1637_handle_t *handle)
{
    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    if (handle->initialized) {
        (void)tm1637_update_display(handle, false);
    }
    handle->initialized = false;
    return DRIVER_OK;
}

int tm1637_clear(tm1637_handle_t *handle)
{
    uint8_t i;

    if (handle == NULL || handle->config.gram == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    for (i = 0U; i < handle->config.digits; i++) {
        handle->config.gram[i] = 0x00U;
    }
    return tm1637_show(handle);
}

int tm1637_show(tm1637_handle_t *handle)
{
    uint8_t i;
    uint8_t digits;

    if (handle == NULL || handle->config.gram == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    digits = handle->config.digits;

    if (tm1637_start(handle) != DRIVER_OK ||
        tm1637_write_byte(handle, TM1637_CMD_DATA_AUTO) != DRIVER_OK ||
        tm1637_stop(handle) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    if (tm1637_start(handle) != DRIVER_OK ||
        tm1637_write_byte(handle, TM1637_CMD_ADDR_BASE) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    for (i = 0U; i < digits; i++) {
        if (tm1637_write_byte(handle, handle->config.gram[i]) != DRIVER_OK) {
            return (int)HAL_IO_ERROR;
        }
    }
    if (tm1637_stop(handle) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    return tm1637_update_display(handle, true);
}

int tm1637_draw_pixel(tm1637_handle_t *handle, uint8_t x, uint8_t y, bool on)
{
    uint8_t mask;

    if (handle == NULL || handle->config.gram == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    if (x >= handle->config.digits || y > 7U) {
        return (int)HAL_INVALID_PARAM;
    }

    mask = (uint8_t)DRIVER_BIT(y);
    if (on) {
        handle->config.gram[x] |= mask;
    } else {
        handle->config.gram[x] &= (uint8_t)~mask;
    }
    return DRIVER_OK;
}

int tm1637_set_brightness(tm1637_handle_t *handle, uint8_t brightness)
{
    if (handle == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    if (brightness > TM1637_BRIGHTNESS_MAX) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->config.brightness = brightness;
    return tm1637_update_display(handle, true);
}

int tm1637_set_display(tm1637_handle_t *handle, bool on)
{
    if (handle == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    return tm1637_update_display(handle, on);
}
