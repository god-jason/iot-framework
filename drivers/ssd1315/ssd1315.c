#include "ssd1315.h"
#include "ssd1315_register.h"
#include "log.h"

#define SSD1315_I2C_CHUNK 16U

static int ssd1315_send(ssd1315_handle_t *handle, uint8_t ctrl,
                        const uint8_t *data, size_t len)
{
    if (handle->config.bus_type == SSD1315_BUS_I2C) {
        uint8_t buf[SSD1315_I2C_CHUNK + 1U];
        size_t offset = 0U;

        while (offset < len) {
            size_t chunk = len - offset;
            int ret;

            if (chunk > SSD1315_I2C_CHUNK) {
                chunk = SSD1315_I2C_CHUNK;
            }
            buf[0] = ctrl;
            for (size_t i = 0U; i < chunk; i++) {
                buf[1U + i] = data[offset + i];
            }
            ret = driver_i2c_write(&handle->config.i2c, buf, chunk + 1U);
            if (ret != DRIVER_OK) {
                return ret;
            }
            offset += chunk;
        }
        return DRIVER_OK;
    }

    driver_spi_select(&handle->config.spi);
    if (driver_gpio_write(handle->config.dc,
                          (ctrl == SSD1315_CTRL_DATA) ? HAL_GPIO_HIGH : HAL_GPIO_LOW) != DRIVER_OK) {
        driver_spi_deselect(&handle->config.spi);
        return (int)HAL_IO_ERROR;
    }
    {
        int ret = driver_spi_write(&handle->config.spi, data, len);
        driver_spi_deselect(&handle->config.spi);
        return ret;
    }
}

static int ssd1315_send_cmd(ssd1315_handle_t *handle, const uint8_t *cmd, size_t len)
{
    return ssd1315_send(handle, SSD1315_CTRL_CMD, cmd, len);
}

static int ssd1315_send_data(ssd1315_handle_t *handle, const uint8_t *data, size_t len)
{
    return ssd1315_send(handle, SSD1315_CTRL_DATA, data, len);
}

static int ssd1315_flush(ssd1315_handle_t *handle)
{
    uint8_t page;

    for (page = 0U; page < SSD1315_PAGES; page++) {
        uint8_t cmd[3];
        size_t offset = (size_t)page * SSD1315_WIDTH;

        cmd[0] = (uint8_t)(SSD1315_CMD_PAGE_START | (page & 0x07U));
        cmd[1] = SSD1315_CMD_COL_LOW;
        cmd[2] = SSD1315_CMD_COL_HIGH;
        if (ssd1315_send_cmd(handle, cmd, sizeof(cmd)) != DRIVER_OK) {
            return (int)HAL_IO_ERROR;
        }
        if (ssd1315_send_data(handle, &handle->config.gram[offset], SSD1315_WIDTH) != DRIVER_OK) {
            return (int)HAL_IO_ERROR;
        }
    }
    return DRIVER_OK;
}

int ssd1315_init(ssd1315_handle_t *handle, const ssd1315_config_t *config)
{
    /* 通用初始化序列，参数需按面板调整 */
    static const uint8_t init_cmds[] = {
        SSD1315_CMD_DISPLAY_OFF,
        SSD1315_CMD_SET_CLOCK, 0x80U,
        SSD1315_CMD_SET_MUX, SSD1315_MUX_RATIO_64,
        SSD1315_CMD_SET_OFFSET, 0x00U,
        SSD1315_CMD_SET_START_LINE,
        SSD1315_CMD_CHARGE_PUMP, SSD1315_CHARGE_PUMP_ON,
        SSD1315_CMD_ADDR_MODE, SSD1315_ADDR_MODE_PAGE,
        SSD1315_CMD_SEG_REMAP,
        SSD1315_CMD_COM_SCAN_DEC,
        SSD1315_CMD_COM_PINS, SSD1315_COM_PINS_64,
        SSD1315_CMD_SET_CONTRAST, SSD1315_CONTRAST_DEFAULT,
        SSD1315_CMD_SET_PRECHARGE, SSD1315_PRECHARGE,
        SSD1315_CMD_SET_VCOM, SSD1315_VCOM_DESELECT,
        SSD1315_CMD_ENTIRE_ON,
        SSD1315_CMD_NORMAL_DISPLAY,
        SSD1315_CMD_DEACT_SCROLL,
        SSD1315_CMD_DISPLAY_ON
    };
    size_t i;

    if (handle == NULL || config == NULL || config->gram == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    if (config->gram_len < SSD1315_GRAM_SIZE) {
        return (int)HAL_INVALID_PARAM;
    }
    if (config->bus_type == SSD1315_BUS_I2C) {
        if (config->i2c.handle == NULL) {
            return (int)HAL_INVALID_PARAM;
        }
    } else {
        if (config->spi.handle == NULL || config->dc == 0U) {
            return (int)HAL_INVALID_PARAM;
        }
        if (driver_gpio_output(config->dc, HAL_GPIO_LOW) != DRIVER_OK) {
            return (int)HAL_IO_ERROR;
        }
        if (config->res != 0U) {
            (void)driver_gpio_output(config->res, HAL_GPIO_LOW);
            driver_delay_ms(SSD1315_RESET_LOW_MS);
            (void)driver_gpio_write(config->res, HAL_GPIO_HIGH);
            driver_delay_ms(SSD1315_RESET_WAIT_MS);
        }
    }

    handle->config = *config;
    handle->width = SSD1315_WIDTH;
    handle->height = SSD1315_HEIGHT;
    handle->initialized = false;

    if (ssd1315_send_cmd(handle, init_cmds, sizeof(init_cmds)) != DRIVER_OK) {
        LOG_E("ssd1315", "init sequence failed");
        return (int)HAL_IO_ERROR;
    }

    for (i = 0U; i < SSD1315_GRAM_SIZE; i++) {
        handle->config.gram[i] = 0x00U;
    }
    if (ssd1315_flush(handle) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }

    handle->initialized = true;
    LOG_I("ssd1315", "init ok (%s)", (config->bus_type == SSD1315_BUS_I2C) ? "I2C" : "SPI");
    return DRIVER_OK;
}

int ssd1315_deinit(ssd1315_handle_t *handle)
{
    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    if (handle->initialized) {
        (void)ssd1315_set_display(handle, false);
    }
    handle->initialized = false;
    return DRIVER_OK;
}

int ssd1315_write_cmd(ssd1315_handle_t *handle, const uint8_t *cmd, size_t len)
{
    if (handle == NULL || cmd == NULL || len == 0U || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    return ssd1315_send_cmd(handle, cmd, len);
}

int ssd1315_write_data(ssd1315_handle_t *handle, const uint8_t *data, size_t len)
{
    if (handle == NULL || data == NULL || len == 0U || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    return ssd1315_send_data(handle, data, len);
}

int ssd1315_clear(ssd1315_handle_t *handle)
{
    return ssd1315_fill(handle, 0x00U);
}

int ssd1315_fill(ssd1315_handle_t *handle, uint8_t pattern)
{
    size_t i;

    if (handle == NULL || handle->config.gram == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    for (i = 0U; i < SSD1315_GRAM_SIZE; i++) {
        handle->config.gram[i] = pattern;
    }
    return ssd1315_flush(handle);
}

int ssd1315_draw_pixel(ssd1315_handle_t *handle, uint8_t x, uint8_t y, bool on)
{
    uint16_t index;
    uint8_t mask;

    if (handle == NULL || handle->config.gram == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    if (x >= handle->width || y >= handle->height) {
        return (int)HAL_INVALID_PARAM;
    }

    if (handle->config.mirror_x) {
        x = (uint8_t)(handle->width - 1U - x);
    }
    if (handle->config.mirror_y) {
        y = (uint8_t)(handle->height - 1U - y);
    }

    index = (uint16_t)(((uint16_t)(y / 8U) * handle->width) + x);
    mask = (uint8_t)DRIVER_BIT(y % 8U);

    if (on) {
        handle->config.gram[index] |= mask;
    } else {
        handle->config.gram[index] &= (uint8_t)~mask;
    }
    return DRIVER_OK;
}

int ssd1315_show(ssd1315_handle_t *handle)
{
    if (handle == NULL || handle->config.gram == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    return ssd1315_flush(handle);
}

int ssd1315_set_contrast(ssd1315_handle_t *handle, uint8_t contrast)
{
    uint8_t cmd[2] = { SSD1315_CMD_SET_CONTRAST, contrast };

    if (handle == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    return ssd1315_send_cmd(handle, cmd, sizeof(cmd));
}

int ssd1315_set_display(ssd1315_handle_t *handle, bool on)
{
    uint8_t cmd = on ? SSD1315_CMD_DISPLAY_ON : SSD1315_CMD_DISPLAY_OFF;

    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    return ssd1315_send_cmd(handle, &cmd, 1U);
}

int ssd1315_set_invert(ssd1315_handle_t *handle, bool invert)
{
    uint8_t cmd = invert ? SSD1315_CMD_INVERT_DISPLAY : SSD1315_CMD_NORMAL_DISPLAY;

    if (handle == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    return ssd1315_send_cmd(handle, &cmd, 1U);
}
