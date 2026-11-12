#include "ssd1306.h"
#include "ssd1306_register.h"
#include "log.h"

static int ssd1306_send_cmd(ssd1306_handle_t *handle, const uint8_t *cmd, size_t len)
{
    if (handle->config.bus_type == SSD1306_BUS_I2C) {
        uint8_t buf[17];
        size_t offset = 0U;

        while (offset < len) {
            size_t chunk = len - offset;
            int ret;

            if (chunk > sizeof(buf) - 1U) {
                chunk = sizeof(buf) - 1U;
            }
            buf[0] = SSD1306_CTRL_CMD;
            for (size_t i = 0U; i < chunk; i++) {
                buf[1 + i] = cmd[offset + i];
            }
            ret = hal_i2c_dev_write(&handle->config.i2c, buf, chunk + 1U);
            if (ret != HAL_OK) {
                return ret;
            }
            offset += chunk;
        }
        return HAL_OK;
    }

    hal_spi_dev_select(&handle->config.spi);
    (void)hal_gpio_write(handle->config.dc, HAL_GPIO_LOW);
    {
        int ret = hal_spi_dev_write(&handle->config.spi, cmd, len);
        hal_spi_dev_deselect(&handle->config.spi);
        return ret;
    }
}

static int ssd1306_send_data(ssd1306_handle_t *handle, const uint8_t *data, size_t len)
{
    if (handle->config.bus_type == SSD1306_BUS_I2C) {
        uint8_t buf[17];
        size_t offset = 0U;

        while (offset < len) {
            size_t chunk = len - offset;
            int ret;

            if (chunk > sizeof(buf) - 1U) {
                chunk = sizeof(buf) - 1U;
            }
            buf[0] = SSD1306_CTRL_DATA;
            for (size_t i = 0U; i < chunk; i++) {
                buf[1 + i] = data[offset + i];
            }
            ret = hal_i2c_dev_write(&handle->config.i2c, buf, chunk + 1U);
            if (ret != HAL_OK) {
                return ret;
            }
            offset += chunk;
        }
        return HAL_OK;
    }

    hal_spi_dev_select(&handle->config.spi);
    (void)hal_gpio_write(handle->config.dc, HAL_GPIO_HIGH);
    {
        int ret = hal_spi_dev_write(&handle->config.spi, data, len);
        hal_spi_dev_deselect(&handle->config.spi);
        return ret;
    }
}

static int ssd1306_set_window(ssd1306_handle_t *handle, uint8_t page)
{
    uint8_t cmd[3];

    cmd[0] = (uint8_t)(SSD1306_CMD_PAGE_START | (page & 0x07U));
    cmd[1] = SSD1306_CMD_COL_LOW;
    cmd[2] = SSD1306_CMD_COL_HIGH;
    return ssd1306_send_cmd(handle, cmd, sizeof(cmd));
}

int ssd1306_init(ssd1306_handle_t *handle, const ssd1306_config_t *config)
{
    static const uint8_t init_cmds[] = {
        SSD1306_CMD_DISPLAY_OFF,
        SSD1306_CMD_SET_CLOCK, SSD1306_CLOCK_DIV,
        SSD1306_CMD_SET_MUX, SSD1306_MUX_RATIO_64,
        SSD1306_CMD_SET_OFFSET, 0x00U,
        SSD1306_CMD_SET_START_LINE,
        SSD1306_CMD_CHARGE_PUMP, SSD1306_CHARGE_PUMP_ON,
        SSD1306_CMD_ADDR_MODE, 0x02U,
        SSD1306_CMD_SEG_REMAP,
        SSD1306_CMD_COM_SCAN_DEC,
        SSD1306_CMD_COM_PINS, SSD1306_COM_PINS_64,
        SSD1306_CMD_SET_CONTRAST, 0x7FU,
        SSD1306_CMD_SET_PRECHARGE, SSD1306_PRECHARGE,
        SSD1306_CMD_SET_VCOM, SSD1306_VCOM_DESELECT,
        SSD1306_CMD_ENTIRE_ON,
        SSD1306_CMD_NORMAL_DISPLAY,
        SSD1306_CMD_DISPLAY_ON
    };

    if (handle == NULL || config == NULL || config->gram == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    if (config->bus_type == SSD1306_BUS_I2C) {
        if (config->i2c.handle == NULL) {
            return (int)HAL_INVALID_PARAM;
        }
    } else {
        if (config->spi.handle == NULL) {
            return (int)HAL_INVALID_PARAM;
        }
        if (hal_gpio_config_output(config->dc, HAL_GPIO_LOW) != HAL_OK) {
            return (int)HAL_IO_ERROR;
        }
        if (config->res != 0U) {
            (void)hal_gpio_config_output(config->res, HAL_GPIO_LOW);
            hal_delay_ms(10U);
            (void)hal_gpio_write(config->res, HAL_GPIO_HIGH);
            hal_delay_ms(10U);
        }
    }

    handle->config = *config;
    handle->width = SSD1306_WIDTH;
    handle->height = SSD1306_HEIGHT;
    handle->initialized = false;

    if (ssd1306_send_cmd(handle, init_cmds, sizeof(init_cmds)) != HAL_OK) {
        LOG_E("ssd1306", "init sequence failed");
        return (int)HAL_IO_ERROR;
    }

    if (ssd1306_clear(handle) != HAL_OK) {
        return (int)HAL_IO_ERROR;
    }

    handle->initialized = true;
    LOG_I("ssd1306", "init ok (%s)", (config->bus_type == SSD1306_BUS_I2C) ? "I2C" : "SPI");
    return HAL_OK;
}

int ssd1306_deinit(ssd1306_handle_t *handle)
{
    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    if (handle->initialized) {
        (void)ssd1306_set_display(handle, false);
    }
    handle->initialized = false;
    return HAL_OK;
}

int ssd1306_write_cmd(ssd1306_handle_t *handle, const uint8_t *cmd, size_t len)
{
    if (handle == NULL || cmd == NULL || len == 0U) {
        return (int)HAL_INVALID_PARAM;
    }
    return ssd1306_send_cmd(handle, cmd, len);
}

int ssd1306_write_data(ssd1306_handle_t *handle, const uint8_t *data, size_t len)
{
    if (handle == NULL || data == NULL || len == 0U) {
        return (int)HAL_INVALID_PARAM;
    }
    return ssd1306_send_data(handle, data, len);
}

int ssd1306_clear(ssd1306_handle_t *handle)
{
    return ssd1306_fill(handle, 0x00U);
}

int ssd1306_fill(ssd1306_handle_t *handle, uint8_t pattern)
{
    uint8_t page;
    uint8_t line[SSD1306_WIDTH];

    if (handle == NULL || handle->config.gram == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    for (size_t i = 0U; i < sizeof(line); i++) {
        line[i] = pattern;
    }

    for (page = 0U; page < SSD1306_PAGES; page++) {
        size_t offset = (size_t)page * SSD1306_WIDTH;

        for (size_t i = 0U; i < SSD1306_WIDTH; i++) {
            handle->config.gram[offset + i] = pattern;
        }
        if (ssd1306_set_window(handle, page) != HAL_OK ||
            ssd1306_send_data(handle, line, sizeof(line)) != HAL_OK) {
            return (int)HAL_IO_ERROR;
        }
    }
    return HAL_OK;
}

int ssd1306_draw_pixel(ssd1306_handle_t *handle, uint8_t x, uint8_t y, bool on)
{
    uint16_t index;
    uint8_t mask;

    if (handle == NULL || handle->config.gram == NULL) {
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

    index = (uint16_t)((y / 8U) * handle->width + x);
    mask = (uint8_t)(1UL << (y % 8U));

    if (on) {
        handle->config.gram[index] |= mask;
    } else {
        handle->config.gram[index] &= (uint8_t)~mask;
    }
    return HAL_OK;
}

int ssd1306_show(ssd1306_handle_t *handle)
{
    uint8_t page;

    if (handle == NULL || handle->config.gram == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }

    for (page = 0U; page < SSD1306_PAGES; page++) {
        size_t offset = (size_t)page * SSD1306_WIDTH;

        if (ssd1306_set_window(handle, page) != HAL_OK ||
            ssd1306_send_data(handle, &handle->config.gram[offset], SSD1306_WIDTH) != HAL_OK) {
            return (int)HAL_IO_ERROR;
        }
    }
    return HAL_OK;
}

int ssd1306_set_contrast(ssd1306_handle_t *handle, uint8_t contrast)
{
    uint8_t cmd[2] = { SSD1306_CMD_SET_CONTRAST, contrast };

    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    return ssd1306_send_cmd(handle, cmd, sizeof(cmd));
}

int ssd1306_set_display(ssd1306_handle_t *handle, bool on)
{
    uint8_t cmd = on ? SSD1306_CMD_DISPLAY_ON : SSD1306_CMD_DISPLAY_OFF;

    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    return ssd1306_send_cmd(handle, &cmd, 1U);
}

int ssd1306_set_invert(ssd1306_handle_t *handle, bool invert)
{
    uint8_t cmd = invert ? SSD1306_CMD_INVERT_DISPLAY : SSD1306_CMD_NORMAL_DISPLAY;

    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    return ssd1306_send_cmd(handle, &cmd, 1U);
}
