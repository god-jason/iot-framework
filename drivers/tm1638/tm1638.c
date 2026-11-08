#include "tm1638.h"
#include "tm1638_register.h"
#include "log.h"

static void tm1638_delay(void)
{
    driver_delay_us(TM1638_DELAY_US);
}

static int tm1638_pin_write(hal_pin_t pin, hal_gpio_level_t level)
{
    return driver_gpio_write(pin, level);
}

/* 写一个字节，低位在前：CLK 低电平期间建立数据，上升沿采样 */
static int tm1638_write_byte(tm1638_handle_t *handle, uint8_t value)
{
    uint8_t i;

    for (i = 0U; i < 8U; i++) {
        if (tm1638_pin_write(handle->config.clk, HAL_GPIO_LOW) != DRIVER_OK) {
            return (int)HAL_IO_ERROR;
        }
        tm1638_delay();
        if (tm1638_pin_write(handle->config.dio,
                             ((value >> i) & 0x01U) != 0U ? HAL_GPIO_HIGH : HAL_GPIO_LOW) != DRIVER_OK) {
            return (int)HAL_IO_ERROR;
        }
        tm1638_delay();
        if (tm1638_pin_write(handle->config.clk, HAL_GPIO_HIGH) != DRIVER_OK) {
            return (int)HAL_IO_ERROR;
        }
        tm1638_delay();
    }
    return DRIVER_OK;
}

/* 读一个字节，低位在前 */
static int tm1638_read_byte(tm1638_handle_t *handle, uint8_t *value)
{
    uint8_t i;
    uint8_t result = 0U;

    if (tm1638_pin_write(handle->config.dio, HAL_GPIO_HIGH) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    if (driver_gpio_input(handle->config.dio, HAL_GPIO_PULLUP) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }

    for (i = 0U; i < 8U; i++) {
        int level;

        if (tm1638_pin_write(handle->config.clk, HAL_GPIO_LOW) != DRIVER_OK) {
            (void)driver_gpio_output(handle->config.dio, HAL_GPIO_HIGH);
            return (int)HAL_IO_ERROR;
        }
        tm1638_delay();
        level = driver_gpio_read(handle->config.dio);
        if (level < 0) {
            (void)driver_gpio_output(handle->config.dio, HAL_GPIO_HIGH);
            return (int)HAL_IO_ERROR;
        }
        if (level != 0) {
            result |= (uint8_t)DRIVER_BIT(i);
        }
        if (tm1638_pin_write(handle->config.clk, HAL_GPIO_HIGH) != DRIVER_OK) {
            (void)driver_gpio_output(handle->config.dio, HAL_GPIO_HIGH);
            return (int)HAL_IO_ERROR;
        }
        tm1638_delay();
    }

    if (driver_gpio_output(handle->config.dio, HAL_GPIO_HIGH) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    *value = result;
    return DRIVER_OK;
}

/* 一次完整事务：STB 拉低 -> 命令字节 -> 可选数据 -> STB 拉高 */
static int tm1638_write_frame(tm1638_handle_t *handle, uint8_t cmd,
                             const uint8_t *data, size_t len)
{
    size_t i;

    if (tm1638_pin_write(handle->config.stb, HAL_GPIO_LOW) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    tm1638_delay();
    if (tm1638_write_byte(handle, cmd) != DRIVER_OK) {
        (void)tm1638_pin_write(handle->config.stb, HAL_GPIO_HIGH);
        return (int)HAL_IO_ERROR;
    }
    for (i = 0U; i < len; i++) {
        if (tm1638_write_byte(handle, data[i]) != DRIVER_OK) {
            (void)tm1638_pin_write(handle->config.stb, HAL_GPIO_HIGH);
            return (int)HAL_IO_ERROR;
        }
    }
    if (tm1638_pin_write(handle->config.stb, HAL_GPIO_HIGH) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    tm1638_delay();
    return DRIVER_OK;
}

static int tm1638_update_display(tm1638_handle_t *handle, bool on)
{
    uint8_t cmd = on
                  ? (uint8_t)(TM1638_CMD_DISPLAY_ON | (handle->config.brightness & TM1638_BRIGHTNESS_MAX))
                  : (uint8_t)TM1638_CMD_DISPLAY_OFF;

    return tm1638_write_frame(handle, cmd, NULL, 0U);
}

int tm1638_init(tm1638_handle_t *handle, const tm1638_config_t *config)
{
    size_t i;

    if (handle == NULL || config == NULL || config->gram == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    if (config->stb == 0U || config->clk == 0U || config->dio == 0U) {
        return (int)HAL_INVALID_PARAM;
    }
    if (config->gram_len < TM1638_REG_COUNT) {
        return (int)HAL_INVALID_PARAM;
    }
    if (config->brightness > TM1638_BRIGHTNESS_MAX) {
        return (int)HAL_INVALID_PARAM;
    }

    handle->config = *config;
    handle->initialized = false;

    if (driver_gpio_output(config->stb, HAL_GPIO_HIGH) != DRIVER_OK ||
        driver_gpio_output(config->clk, HAL_GPIO_HIGH) != DRIVER_OK ||
        driver_gpio_output(config->dio, HAL_GPIO_HIGH) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }

    for (i = 0U; i < TM1638_REG_COUNT; i++) {
        handle->config.gram[i] = 0x00U;
    }

    /* 清显示 -> 打开显示（内部写寄存器接口要求 initialized 为 true） */
    handle->initialized = true;
    if (tm1638_write_regs(handle, handle->config.gram, TM1638_REG_COUNT) != DRIVER_OK ||
        tm1638_update_display(handle, true) != DRIVER_OK) {
        handle->initialized = false;
        LOG_E("tm1638", "init sequence failed");
        return (int)HAL_IO_ERROR;
    }

    LOG_I("tm1638", "init ok");
    return DRIVER_OK;
}

int tm1638_deinit(tm1638_handle_t *handle)
{
    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    if (handle->initialized) {
        (void)tm1638_update_display(handle, false);
    }
    handle->initialized = false;
    return DRIVER_OK;
}

int tm1638_write_regs(tm1638_handle_t *handle, const uint8_t *regs, size_t len)
{
    if (handle == NULL || regs == NULL || len == 0U || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    if (len > TM1638_REG_COUNT) {
        return (int)HAL_INVALID_PARAM;
    }
    if (tm1638_write_frame(handle, TM1638_CMD_DATA_AUTO, NULL, 0U) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    return tm1638_write_frame(handle, TM1638_CMD_ADDR_BASE, regs, len);
}

int tm1638_show(tm1638_handle_t *handle)
{
    if (handle == NULL || handle->config.gram == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    return tm1638_write_regs(handle, handle->config.gram, TM1638_REG_COUNT);
}

int tm1638_clear(tm1638_handle_t *handle)
{
    size_t i;

    if (handle == NULL || handle->config.gram == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    for (i = 0U; i < TM1638_REG_COUNT; i++) {
        handle->config.gram[i] = 0x00U;
    }
    return tm1638_show(handle);
}

int tm1638_draw_pixel(tm1638_handle_t *handle, uint8_t x, uint8_t y, bool on)
{
    uint8_t mask;

    if (handle == NULL || handle->config.gram == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    if (x >= TM1638_REG_COUNT || y > 7U) {
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

int tm1638_read_keys(tm1638_handle_t *handle, uint8_t keys[TM1638_KEY_BYTES])
{
    uint8_t i;

    if (handle == NULL || keys == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }

    if (tm1638_pin_write(handle->config.stb, HAL_GPIO_LOW) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    tm1638_delay();
    if (tm1638_write_byte(handle, TM1638_CMD_READ_KEYS) != DRIVER_OK) {
        (void)tm1638_pin_write(handle->config.stb, HAL_GPIO_HIGH);
        return (int)HAL_IO_ERROR;
    }
    for (i = 0U; i < TM1638_KEY_BYTES; i++) {
        if (tm1638_read_byte(handle, &keys[i]) != DRIVER_OK) {
            (void)tm1638_pin_write(handle->config.stb, HAL_GPIO_HIGH);
            return (int)HAL_IO_ERROR;
        }
    }
    if (tm1638_pin_write(handle->config.stb, HAL_GPIO_HIGH) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    return DRIVER_OK;
}

int tm1638_set_brightness(tm1638_handle_t *handle, uint8_t brightness)
{
    if (handle == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    if (brightness > TM1638_BRIGHTNESS_MAX) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->config.brightness = brightness;
    return tm1638_update_display(handle, true);
}

int tm1638_set_display(tm1638_handle_t *handle, bool on)
{
    if (handle == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    return tm1638_update_display(handle, on);
}
