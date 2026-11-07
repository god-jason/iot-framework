#include "st7789.h"
#include "st7789_register.h"
#include "iot_logger.h"

static int st7789_send(st7789_handle_t *handle, bool is_data,
                       const uint8_t *data, size_t len)
{
    int ret;

    driver_spi_select(&handle->config.spi);
    if (driver_gpio_write(handle->config.dc,
                          is_data ? PORT_GPIO_HIGH : PORT_GPIO_LOW) != DRIVER_OK) {
        driver_spi_deselect(&handle->config.spi);
        return (int)PORT_IO_ERROR;
    }
    ret = driver_spi_write(&handle->config.spi, data, len);
    driver_spi_deselect(&handle->config.spi);
    return ret;
}

static int st7789_write_cmd_byte(st7789_handle_t *handle, uint8_t cmd)
{
    return st7789_send(handle, false, &cmd, 1U);
}

static int st7789_set_window(st7789_handle_t *handle, uint16_t x0, uint16_t y0,
                             uint16_t x1, uint16_t y1)
{
    uint8_t cmd[4];
    uint16_t xs = (uint16_t)(x0 + handle->config.x_offset);
    uint16_t xe = (uint16_t)(x1 + handle->config.x_offset);
    uint16_t ys = (uint16_t)(y0 + handle->config.y_offset);
    uint16_t ye = (uint16_t)(y1 + handle->config.y_offset);

    if (st7789_write_cmd_byte(handle, ST7789_CMD_CASET) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    cmd[0] = (uint8_t)(xs >> 8);
    cmd[1] = (uint8_t)(xs & 0xFFU);
    cmd[2] = (uint8_t)(xe >> 8);
    cmd[3] = (uint8_t)(xe & 0xFFU);
    if (st7789_send(handle, true, cmd, sizeof(cmd)) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }

    if (st7789_write_cmd_byte(handle, ST7789_CMD_RASET) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    cmd[0] = (uint8_t)(ys >> 8);
    cmd[1] = (uint8_t)(ys & 0xFFU);
    cmd[2] = (uint8_t)(ye >> 8);
    cmd[3] = (uint8_t)(ye & 0xFFU);
    if (st7789_send(handle, true, cmd, sizeof(cmd)) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    return DRIVER_OK;
}

/* 整屏推送：单次 CS 期间逐行发送，避免构造大缓冲 */
static int st7789_push(st7789_handle_t *handle)
{
    size_t stride = (size_t)handle->width * 2U;
    uint16_t row;
    int ret = DRIVER_OK;

    if (st7789_set_window(handle, 0U, 0U,
                          (uint16_t)(handle->width - 1U),
                          (uint16_t)(handle->height - 1U)) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    if (st7789_write_cmd_byte(handle, ST7789_CMD_RAMWR) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }

    driver_spi_select(&handle->config.spi);
    if (driver_gpio_write(handle->config.dc, PORT_GPIO_HIGH) != DRIVER_OK) {
        driver_spi_deselect(&handle->config.spi);
        return (int)PORT_IO_ERROR;
    }
    for (row = 0U; row < handle->height; row++) {
        ret = driver_spi_write(&handle->config.spi,
                               &handle->config.gram[(size_t)row * stride], stride);
        if (ret != DRIVER_OK) {
            break;
        }
    }
    driver_spi_deselect(&handle->config.spi);
    return (ret == DRIVER_OK) ? DRIVER_OK : (int)PORT_IO_ERROR;
}

static void st7789_gram_fill(st7789_handle_t *handle, uint16_t color)
{
    size_t total = (size_t)handle->width * handle->height;
    size_t i;

    for (i = 0U; i < total; i++) {
        handle->config.gram[i * 2U] = (uint8_t)(color >> 8);
        handle->config.gram[i * 2U + 1U] = (uint8_t)(color & 0xFFU);
    }
}

int st7789_init(st7789_handle_t *handle, const st7789_config_t *config)
{
    /* 通用初始化序列，PORCTRL / GAMMA 参数需按面板调整 */
    static const uint8_t porctrl[] = { 0x0CU, 0x0CU, 0x00U, 0x33U, 0x33U };
    static const uint8_t pwctrl1[] = { 0xA4U, 0xA1U };
    static const uint8_t pgamma[] = {
        0xD0U, 0x08U, 0x11U, 0x08U, 0x0CU, 0x15U, 0x39U,
        0x33U, 0x50U, 0x36U, 0x13U, 0x14U, 0x29U, 0x2DU
    };
    static const uint8_t ngamma[] = {
        0xD0U, 0x08U, 0x10U, 0x08U, 0x06U, 0x06U, 0x39U,
        0x44U, 0x51U, 0x0BU, 0x16U, 0x14U, 0x2FU, 0x31U
    };
    uint8_t madctl;
    uint8_t colmod = ST7789_COLMOD_RGB565;

    if (handle == NULL || config == NULL || config->gram == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    if (config->spi.handle == NULL || config->dc == 0U) {
        return (int)PORT_INVALID_PARAM;
    }

    handle->width = (config->width != 0U) ? config->width : (uint16_t)ST7789_WIDTH;
    handle->height = (config->height != 0U) ? config->height : (uint16_t)ST7789_HEIGHT;
    if (config->gram_len < ((size_t)handle->width * handle->height * 2U)) {
        return (int)PORT_INVALID_PARAM;
    }

    handle->config = *config;
    handle->config.width = handle->width;
    handle->config.height = handle->height;
    handle->initialized = false;

    if (driver_gpio_output(config->dc, PORT_GPIO_LOW) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    if (config->bl != 0U) {
        if (driver_gpio_output(config->bl, PORT_GPIO_HIGH) != DRIVER_OK) {
            return (int)PORT_IO_ERROR;
        }
    }
    if (config->res != 0U) {
        (void)driver_gpio_output(config->res, PORT_GPIO_LOW);
        driver_delay_ms(ST7789_RESET_LOW_MS);
        (void)driver_gpio_write(config->res, PORT_GPIO_HIGH);
        driver_delay_ms(ST7789_RESET_WAIT_MS);
    }

    /* SWRESET -> SLPOUT */
    if (st7789_write_cmd_byte(handle, ST7789_CMD_SWRESET) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    driver_delay_ms(ST7789_RESET_WAIT_MS);
    if (st7789_write_cmd_byte(handle, ST7789_CMD_SLPOUT) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    driver_delay_ms(ST7789_SLPOUT_MS);

    madctl = 0x00U;
    if (config->mirror_x) {
        madctl |= ST7789_MADCTL_MX;
    }
    if (config->mirror_y) {
        madctl |= ST7789_MADCTL_MY;
    }
    if (config->bgr) {
        madctl |= ST7789_MADCTL_BGR;
    }

    if (st7789_write_cmd_byte(handle, ST7789_CMD_COLMOD) != DRIVER_OK ||
        st7789_send(handle, true, &colmod, 1U) != DRIVER_OK ||
        st7789_write_cmd_byte(handle, ST7789_CMD_MADCTL) != DRIVER_OK ||
        st7789_send(handle, true, &madctl, 1U) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }

    if (st7789_write_cmd_byte(handle, ST7789_CMD_PORCTRL) != DRIVER_OK ||
        st7789_send(handle, true, porctrl, sizeof(porctrl)) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }

    {
        uint8_t v = 0x35U;      /* GCTRL */
        uint8_t vcoms = 0x19U;
        uint8_t lcm = 0x2CU;
        uint8_t vdvvrhen = 0x01U;
        uint8_t vrhs = 0x12U;
        uint8_t vdvs = 0x20U;
        uint8_t frctrl = 0x0FU;

        if (st7789_write_cmd_byte(handle, ST7789_CMD_GCTRL) != DRIVER_OK ||
            st7789_send(handle, true, &v, 1U) != DRIVER_OK ||
            st7789_write_cmd_byte(handle, ST7789_CMD_VCOMS) != DRIVER_OK ||
            st7789_send(handle, true, &vcoms, 1U) != DRIVER_OK ||
            st7789_write_cmd_byte(handle, ST7789_CMD_LCMCTRL) != DRIVER_OK ||
            st7789_send(handle, true, &lcm, 1U) != DRIVER_OK ||
            st7789_write_cmd_byte(handle, ST7789_CMD_VDVVRHEN) != DRIVER_OK ||
            st7789_send(handle, true, &vdvvrhen, 1U) != DRIVER_OK ||
            st7789_write_cmd_byte(handle, ST7789_CMD_VRHS) != DRIVER_OK ||
            st7789_send(handle, true, &vrhs, 1U) != DRIVER_OK ||
            st7789_write_cmd_byte(handle, ST7789_CMD_VDVS) != DRIVER_OK ||
            st7789_send(handle, true, &vdvs, 1U) != DRIVER_OK ||
            st7789_write_cmd_byte(handle, ST7789_CMD_FRCTRL2) != DRIVER_OK ||
            st7789_send(handle, true, &frctrl, 1U) != DRIVER_OK ||
            st7789_write_cmd_byte(handle, ST7789_CMD_PWCTRL1) != DRIVER_OK ||
            st7789_send(handle, true, pwctrl1, sizeof(pwctrl1)) != DRIVER_OK ||
            st7789_write_cmd_byte(handle, ST7789_CMD_PVGAMCTRL) != DRIVER_OK ||
            st7789_send(handle, true, pgamma, sizeof(pgamma)) != DRIVER_OK ||
            st7789_write_cmd_byte(handle, ST7789_CMD_NVGAMCTRL) != DRIVER_OK ||
            st7789_send(handle, true, ngamma, sizeof(ngamma)) != DRIVER_OK) {
            return (int)PORT_IO_ERROR;
        }
    }

    if (config->invert) {
        if (st7789_write_cmd_byte(handle, ST7789_CMD_INVON) != DRIVER_OK) {
            return (int)PORT_IO_ERROR;
        }
    } else if (st7789_write_cmd_byte(handle, ST7789_CMD_INVOFF) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    if (st7789_write_cmd_byte(handle, ST7789_CMD_NORON) != DRIVER_OK ||
        st7789_write_cmd_byte(handle, ST7789_CMD_DISPON) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    driver_delay_ms(ST7789_DISPON_MS);

    st7789_gram_fill(handle, ST7789_COLOR_BLACK);
    if (st7789_push(handle) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }

    handle->initialized = true;
    LOG_I("st7789", "init ok (%ux%u)", (unsigned)handle->width, (unsigned)handle->height);
    return DRIVER_OK;
}

int st7789_deinit(st7789_handle_t *handle)
{
    if (handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    if (handle->initialized) {
        (void)st7789_write_cmd_byte(handle, ST7789_CMD_DISPOFF);
        (void)st7789_write_cmd_byte(handle, ST7789_CMD_SLPIN);
        if (handle->config.bl != 0U) {
            (void)driver_gpio_write(handle->config.bl, PORT_GPIO_LOW);
        }
    }
    handle->initialized = false;
    return DRIVER_OK;
}

int st7789_write_cmd(st7789_handle_t *handle, const uint8_t *cmd, size_t len)
{
    if (handle == NULL || cmd == NULL || len == 0U || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    return st7789_send(handle, false, cmd, len);
}

int st7789_write_data(st7789_handle_t *handle, const uint8_t *data, size_t len)
{
    if (handle == NULL || data == NULL || len == 0U || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    return st7789_send(handle, true, data, len);
}

int st7789_fill_color(st7789_handle_t *handle, uint16_t color)
{
    if (handle == NULL || handle->config.gram == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    st7789_gram_fill(handle, color);
    return st7789_push(handle);
}

int st7789_clear(st7789_handle_t *handle)
{
    return st7789_fill_color(handle, ST7789_COLOR_BLACK);
}

int st7789_draw_pixel(st7789_handle_t *handle, uint16_t x, uint16_t y, uint16_t color)
{
    size_t index;

    if (handle == NULL || handle->config.gram == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    if (x >= handle->width || y >= handle->height) {
        return (int)PORT_INVALID_PARAM;
    }
    if (handle->config.mirror_x) {
        x = (uint16_t)(handle->width - 1U - x);
    }
    if (handle->config.mirror_y) {
        y = (uint16_t)(handle->height - 1U - y);
    }

    index = ((size_t)y * handle->width + x) * 2U;
    handle->config.gram[index] = (uint8_t)(color >> 8);
    handle->config.gram[index + 1U] = (uint8_t)(color & 0xFFU);
    return DRIVER_OK;
}

int st7789_fill_rect(st7789_handle_t *handle, uint16_t x, uint16_t y,
                     uint16_t w, uint16_t h, uint16_t color)
{
    uint16_t i;
    uint16_t j;

    if (handle == NULL || handle->config.gram == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    if (x >= handle->width || y >= handle->height) {
        return (int)PORT_INVALID_PARAM;
    }
    if ((uint32_t)x + w > handle->width) {
        w = (uint16_t)(handle->width - x);
    }
    if ((uint32_t)y + h > handle->height) {
        h = (uint16_t)(handle->height - y);
    }

    for (j = 0U; j < h; j++) {
        for (i = 0U; i < w; i++) {
            size_t index = (((size_t)(y + j) * handle->width) + x + i) * 2U;

            handle->config.gram[index] = (uint8_t)(color >> 8);
            handle->config.gram[index + 1U] = (uint8_t)(color & 0xFFU);
        }
    }
    return DRIVER_OK;
}

int st7789_show(st7789_handle_t *handle)
{
    if (handle == NULL || handle->config.gram == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    return st7789_push(handle);
}

int st7789_set_backlight(st7789_handle_t *handle, bool on)
{
    if (handle == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    if (handle->config.bl == 0U) {
        return (int)PORT_NOT_SUPPORTED;
    }
    return (driver_gpio_write(handle->config.bl,
                              on ? PORT_GPIO_HIGH : PORT_GPIO_LOW) == DRIVER_OK)
           ? DRIVER_OK : (int)PORT_IO_ERROR;
}

int st7789_set_invert(st7789_handle_t *handle, bool invert)
{
    if (handle == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    return st7789_write_cmd_byte(handle, invert ? ST7789_CMD_INVON : ST7789_CMD_INVOFF);
}

int st7789_set_sleep(st7789_handle_t *handle, bool sleep)
{
    if (handle == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    if (sleep) {
        return st7789_write_cmd_byte(handle, ST7789_CMD_SLPIN);
    }
    if (st7789_write_cmd_byte(handle, ST7789_CMD_SLPOUT) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    driver_delay_ms(ST7789_SLPOUT_MS);
    return DRIVER_OK;
}
