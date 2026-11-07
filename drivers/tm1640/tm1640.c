#include "tm1640.h"
#include "tm1640_register.h"
#include "log.h"

static void tm1640_delay(void)
{
    driver_delay_us(TM1640_DELAY_US);
}

/* 写一个字节，低位在前：SCLK 低电平期间建立数据，上升沿采样 */
static int tm1640_write_byte(tm1640_handle_t *handle, uint8_t value)
{
    uint8_t i;

    for (i = 0U; i < 8U; i++) {
        if (driver_gpio_write(handle->config.sclk, PORT_GPIO_LOW) != DRIVER_OK) {
            return (int)PORT_IO_ERROR;
        }
        tm1640_delay();
        if (driver_gpio_write(handle->config.din,
                              ((value >> i) & 0x01U) != 0U ? PORT_GPIO_HIGH : PORT_GPIO_LOW) != DRIVER_OK) {
            return (int)PORT_IO_ERROR;
        }
        tm1640_delay();
        if (driver_gpio_write(handle->config.sclk, PORT_GPIO_HIGH) != DRIVER_OK) {
            return (int)PORT_IO_ERROR;
        }
        tm1640_delay();
    }
    return DRIVER_OK;
}

static int tm1640_send_cmd(tm1640_handle_t *handle, uint8_t cmd)
{
    if (driver_gpio_write(handle->config.sclk, PORT_GPIO_HIGH) != DRIVER_OK ||
        driver_gpio_write(handle->config.din, PORT_GPIO_HIGH) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    tm1640_delay();
    if (tm1640_write_byte(handle, cmd) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    /* 结束条件：DIN 低 -> SCLK 高 -> DIN 高 */
    if (driver_gpio_write(handle->config.din, PORT_GPIO_LOW) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    tm1640_delay();
    if (driver_gpio_write(handle->config.sclk, PORT_GPIO_HIGH) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    tm1640_delay();
    if (driver_gpio_write(handle->config.din, PORT_GPIO_HIGH) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    tm1640_delay();
    return DRIVER_OK;
}

static int tm1640_update_display(tm1640_handle_t *handle, bool on)
{
    uint8_t cmd = on
                  ? (uint8_t)(TM1640_CMD_DISPLAY_ON | (handle->config.brightness & TM1640_BRIGHTNESS_MAX))
                  : (uint8_t)TM1640_CMD_DISPLAY_OFF;

    return tm1640_send_cmd(handle, cmd);
}

int tm1640_init(tm1640_handle_t *handle, const tm1640_config_t *config)
{
    size_t i;

    if (handle == NULL || config == NULL || config->gram == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    if (config->sclk == 0U || config->din == 0U) {
        return (int)PORT_INVALID_PARAM;
    }
    if (config->gram_len < TM1640_GRID_COUNT) {
        return (int)PORT_INVALID_PARAM;
    }
    if (config->brightness > TM1640_BRIGHTNESS_MAX) {
        return (int)PORT_INVALID_PARAM;
    }

    handle->config = *config;
    handle->initialized = false;

    if (driver_gpio_output(config->sclk, PORT_GPIO_HIGH) != DRIVER_OK ||
        driver_gpio_output(config->din, PORT_GPIO_HIGH) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }

    for (i = 0U; i < TM1640_GRID_COUNT; i++) {
        handle->config.gram[i] = 0x00U;
    }

    /* 清显示 -> 打开显示（show 接口要求 initialized 为 true） */
    handle->initialized = true;
    if (tm1640_show(handle) != DRIVER_OK ||
        tm1640_update_display(handle, true) != DRIVER_OK) {
        handle->initialized = false;
        LOG_E("tm1640", "init sequence failed");
        return (int)PORT_IO_ERROR;
    }

    LOG_I("tm1640", "init ok");
    return DRIVER_OK;
}

int tm1640_deinit(tm1640_handle_t *handle)
{
    if (handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    if (handle->initialized) {
        (void)tm1640_update_display(handle, false);
    }
    handle->initialized = false;
    return DRIVER_OK;
}

int tm1640_show(tm1640_handle_t *handle)
{
    size_t i;

    if (handle == NULL || handle->config.gram == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }

    /* 数据传输命令：自动地址递增 */
    if (tm1640_send_cmd(handle, TM1640_CMD_DATA_AUTO) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    /* 起始地址 0，随后连续写入 16 字节 */
    if (tm1640_send_cmd(handle, TM1640_CMD_ADDR_BASE) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    for (i = 0U; i < TM1640_GRID_COUNT; i++) {
        if (tm1640_write_byte(handle, handle->config.gram[i]) != DRIVER_OK) {
            return (int)PORT_IO_ERROR;
        }
    }
    return tm1640_update_display(handle, true);
}

int tm1640_fill(tm1640_handle_t *handle, uint8_t pattern)
{
    size_t i;

    if (handle == NULL || handle->config.gram == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    for (i = 0U; i < TM1640_GRID_COUNT; i++) {
        handle->config.gram[i] = pattern;
    }
    return tm1640_show(handle);
}

int tm1640_clear(tm1640_handle_t *handle)
{
    return tm1640_fill(handle, 0x00U);
}

int tm1640_draw_pixel(tm1640_handle_t *handle, uint8_t x, uint8_t y, bool on)
{
    uint8_t mask;

    if (handle == NULL || handle->config.gram == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    if (x >= TM1640_GRID_COUNT || y > 7U) {
        return (int)PORT_INVALID_PARAM;
    }

    mask = (uint8_t)DRIVER_BIT(y);
    if (on) {
        handle->config.gram[x] |= mask;
    } else {
        handle->config.gram[x] &= (uint8_t)~mask;
    }
    return DRIVER_OK;
}

int tm1640_set_brightness(tm1640_handle_t *handle, uint8_t brightness)
{
    if (handle == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    if (brightness > TM1640_BRIGHTNESS_MAX) {
        return (int)PORT_INVALID_PARAM;
    }
    handle->config.brightness = brightness;
    return tm1640_update_display(handle, true);
}

int tm1640_set_display(tm1640_handle_t *handle, bool on)
{
    if (handle == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    return tm1640_update_display(handle, on);
}
