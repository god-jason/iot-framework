#include "ssd1309.h"
#include "ssd1309_register.h"
#include "iot_logger.h"

/* 单次 I2C 传输携带的数据字节数（另加 1 字节控制字节） */
#define SSD1309_I2C_CHUNK 16U

static int ssd1309_send_cmd(ssd1309_handle_t *handle, const uint8_t *cmd, size_t len)
{
    if (handle->config.bus_type == SSD1309_BUS_I2C) {
        uint8_t buf[SSD1309_I2C_CHUNK + 1U];
        size_t offset = 0U;

        while (offset < len) {
            size_t chunk = len - offset;
            int ret;

            if (chunk > SSD1309_I2C_CHUNK) {
                chunk = SSD1309_I2C_CHUNK;
            }
            buf[0] = SSD1309_CTRL_CMD;
            for (size_t i = 0U; i < chunk; i++) {
                buf[1U + i] = cmd[offset + i];
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
    if (driver_gpio_write(handle->config.dc, PORT_GPIO_LOW) != DRIVER_OK) {
        driver_spi_deselect(&handle->config.spi);
        return (int)PORT_IO_ERROR;
    }
    {
        int ret = driver_spi_write(&handle->config.spi, cmd, len);
        driver_spi_deselect(&handle->config.spi);
        return ret;
    }
}

static int ssd1309_send_data(ssd1309_handle_t *handle, const uint8_t *data, size_t len)
{
    if (handle->config.bus_type == SSD1309_BUS_I2C) {
        uint8_t buf[SSD1309_I2C_CHUNK + 1U];
        size_t offset = 0U;

        while (offset < len) {
            size_t chunk = len - offset;
            int ret;

            if (chunk > SSD1309_I2C_CHUNK) {
                chunk = SSD1309_I2C_CHUNK;
            }
            buf[0] = SSD1309_CTRL_DATA;
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
    if (driver_gpio_write(handle->config.dc, PORT_GPIO_HIGH) != DRIVER_OK) {
        driver_spi_deselect(&handle->config.spi);
        return (int)PORT_IO_ERROR;
    }
    {
        int ret = driver_spi_write(&handle->config.spi, data, len);
        driver_spi_deselect(&handle->config.spi);
        return ret;
    }
}

/* 设置页寻址窗口：页号 + 列地址 0 */
static int ssd1309_set_window(ssd1309_handle_t *handle, uint8_t page)
{
    uint8_t cmd[3];

    cmd[0] = (uint8_t)(SSD1309_CMD_PAGE_START | (page & 0x07U));
    cmd[1] = SSD1309_CMD_COL_LOW;
    cmd[2] = SSD1309_CMD_COL_HIGH;
    return ssd1309_send_cmd(handle, cmd, sizeof(cmd));
}

static int ssd1309_flush(ssd1309_handle_t *handle)
{
    uint8_t page;

    for (page = 0U; page < SSD1309_PAGES; page++) {
        size_t offset = (size_t)page * SSD1309_WIDTH;

        if (ssd1309_set_window(handle, page) != DRIVER_OK) {
            return (int)PORT_IO_ERROR;
        }
        if (ssd1309_send_data(handle, &handle->config.gram[offset], SSD1309_WIDTH) != DRIVER_OK) {
            return (int)PORT_IO_ERROR;
        }
    }
    return DRIVER_OK;
}

static void ssd1309_gram_fill(ssd1309_handle_t *handle, uint8_t pattern)
{
    size_t i;

    for (i = 0U; i < SSD1309_GRAM_SIZE; i++) {
        handle->config.gram[i] = pattern;
    }
}

int ssd1309_init(ssd1309_handle_t *handle, const ssd1309_config_t *config)
{
    static const uint8_t init_cmds[] = {
        SSD1309_CMD_DISPLAY_OFF,
        SSD1309_CMD_SET_CLOCK, SSD1309_CLOCK_DIV,
        SSD1309_CMD_SET_MUX, SSD1309_MUX_RATIO_64,
        SSD1309_CMD_SET_OFFSET, 0x00U,
        SSD1309_CMD_SET_START_LINE,
        SSD1309_CMD_CHARGE_PUMP, SSD1309_CHARGE_PUMP_ON,
        SSD1309_CMD_ADDR_MODE, SSD1309_ADDR_MODE_PAGE,
        SSD1309_CMD_SEG_REMAP,
        SSD1309_CMD_COM_SCAN_DEC,
        SSD1309_CMD_COM_PINS, SSD1309_COM_PINS_64,
        SSD1309_CMD_SET_CONTRAST, SSD1309_CONTRAST_DEFAULT,
        SSD1309_CMD_SET_PRECHARGE, SSD1309_PRECHARGE,
        SSD1309_CMD_SET_VCOM, SSD1309_VCOM_DESELECT,
        SSD1309_CMD_ENTIRE_ON,
        SSD1309_CMD_NORMAL_DISPLAY,
        SSD1309_CMD_DEACT_SCROLL,
        SSD1309_CMD_DISPLAY_ON
    };

    if (handle == NULL || config == NULL || config->gram == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    if (config->gram_len < SSD1309_GRAM_SIZE) {
        return (int)PORT_INVALID_PARAM;
    }
    if (config->bus_type == SSD1309_BUS_I2C) {
        if (config->i2c.handle == NULL) {
            return (int)PORT_INVALID_PARAM;
        }
    } else {
        if (config->spi.handle == NULL || config->dc == 0U) {
            return (int)PORT_INVALID_PARAM;
        }
        if (driver_gpio_output(config->dc, PORT_GPIO_LOW) != DRIVER_OK) {
            return (int)PORT_IO_ERROR;
        }
        if (config->res != 0U) {
            (void)driver_gpio_output(config->res, PORT_GPIO_LOW);
            driver_delay_ms(SSD1309_RESET_LOW_MS);
            (void)driver_gpio_write(config->res, PORT_GPIO_HIGH);
            driver_delay_ms(SSD1309_RESET_WAIT_MS);
        }
    }

    handle->config = *config;
    handle->width = SSD1309_WIDTH;
    handle->height = SSD1309_HEIGHT;
    handle->initialized = false;

    if (ssd1309_send_cmd(handle, init_cmds, sizeof(init_cmds)) != DRIVER_OK) {
        LOG_E("ssd1309", "init sequence failed");
        return (int)PORT_IO_ERROR;
    }

    ssd1309_gram_fill(handle, 0x00U);
    if (ssd1309_flush(handle) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }

    handle->initialized = true;
    LOG_I("ssd1309", "init ok (%s)", (config->bus_type == SSD1309_BUS_I2C) ? "I2C" : "SPI");
    return DRIVER_OK;
}

int ssd1309_deinit(ssd1309_handle_t *handle)
{
    if (handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    if (handle->initialized) {
        (void)ssd1309_set_display(handle, false);
    }
    handle->initialized = false;
    return DRIVER_OK;
}

int ssd1309_write_cmd(ssd1309_handle_t *handle, const uint8_t *cmd, size_t len)
{
    if (handle == NULL || cmd == NULL || len == 0U || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    return ssd1309_send_cmd(handle, cmd, len);
}

int ssd1309_write_data(ssd1309_handle_t *handle, const uint8_t *data, size_t len)
{
    if (handle == NULL || data == NULL || len == 0U || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    return ssd1309_send_data(handle, data, len);
}

int ssd1309_clear(ssd1309_handle_t *handle)
{
    return ssd1309_fill(handle, 0x00U);
}

int ssd1309_fill(ssd1309_handle_t *handle, uint8_t pattern)
{
    if (handle == NULL || handle->config.gram == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    ssd1309_gram_fill(handle, pattern);
    return ssd1309_flush(handle);
}

int ssd1309_draw_pixel(ssd1309_handle_t *handle, uint8_t x, uint8_t y, bool on)
{
    uint16_t index;
    uint8_t mask;

    if (handle == NULL || handle->config.gram == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    if (x >= handle->width || y >= handle->height) {
        return (int)PORT_INVALID_PARAM;
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

int ssd1309_show(ssd1309_handle_t *handle)
{
    if (handle == NULL || handle->config.gram == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    return ssd1309_flush(handle);
}

int ssd1309_set_contrast(ssd1309_handle_t *handle, uint8_t contrast)
{
    uint8_t cmd[2] = { SSD1309_CMD_SET_CONTRAST, contrast };

    if (handle == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    return ssd1309_send_cmd(handle, cmd, sizeof(cmd));
}

int ssd1309_set_display(ssd1309_handle_t *handle, bool on)
{
    uint8_t cmd = on ? SSD1309_CMD_DISPLAY_ON : SSD1309_CMD_DISPLAY_OFF;

    if (handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    return ssd1309_send_cmd(handle, &cmd, 1U);
}

int ssd1309_set_invert(ssd1309_handle_t *handle, bool invert)
{
    uint8_t cmd = invert ? SSD1309_CMD_INVERT_DISPLAY : SSD1309_CMD_NORMAL_DISPLAY;

    if (handle == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    return ssd1309_send_cmd(handle, &cmd, 1U);
}
