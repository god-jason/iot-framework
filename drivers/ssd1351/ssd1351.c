#include "ssd1351.h"
#include "ssd1351_register.h"
#include "log.h"

static int ssd1351_send(ssd1351_handle_t *handle, bool is_data,
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

static int ssd1351_cmd(ssd1351_handle_t *handle, uint8_t cmd)
{
    return ssd1351_send(handle, false, &cmd, 1U);
}

static int ssd1351_cmd_arg(ssd1351_handle_t *handle, uint8_t cmd,
                           const uint8_t *arg, size_t len)
{
    if (ssd1351_cmd(handle, cmd) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    if (ssd1351_send(handle, true, arg, len) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    return DRIVER_OK;
}

/* 设置列/行窗口（0..127） */
static int ssd1351_set_window(ssd1351_handle_t *handle, uint8_t x0, uint8_t y0,
                              uint8_t x1, uint8_t y1)
{
    uint8_t col[2] = { x0, x1 };
    uint8_t row[2] = { y0, y1 };

    if (ssd1351_cmd_arg(handle, SSD1351_CMD_SET_COLUMN, col, sizeof(col)) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    return ssd1351_cmd_arg(handle, SSD1351_CMD_SET_ROW, row, sizeof(row));
}

/* 整屏推送：逐行发送，单次 CS 期间完成 */
static int ssd1351_push(ssd1351_handle_t *handle)
{
    size_t stride = (size_t)SSD1351_WIDTH * 2U;
    uint8_t row;
    int ret = DRIVER_OK;

    if (ssd1351_set_window(handle, 0U, 0U,
                           (uint8_t)(SSD1351_WIDTH - 1U),
                           (uint8_t)(SSD1351_HEIGHT - 1U)) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    if (ssd1351_cmd(handle, SSD1351_CMD_WRITE_RAM) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }

    driver_spi_select(&handle->config.spi);
    if (driver_gpio_write(handle->config.dc, PORT_GPIO_HIGH) != DRIVER_OK) {
        driver_spi_deselect(&handle->config.spi);
        return (int)PORT_IO_ERROR;
    }
    for (row = 0U; row < SSD1351_HEIGHT; row++) {
        ret = driver_spi_write(&handle->config.spi,
                               &handle->config.gram[(size_t)row * stride], stride);
        if (ret != DRIVER_OK) {
            break;
        }
    }
    driver_spi_deselect(&handle->config.spi);
    return (ret == DRIVER_OK) ? DRIVER_OK : (int)PORT_IO_ERROR;
}

static void ssd1351_gram_fill(ssd1351_handle_t *handle, uint16_t color)
{
    size_t total = (size_t)SSD1351_WIDTH * SSD1351_HEIGHT;
    size_t i;

    for (i = 0U; i < total; i++) {
        handle->config.gram[i * 2U] = (uint8_t)(color >> 8);
        handle->config.gram[i * 2U + 1U] = (uint8_t)(color & 0xFFU);
    }
}

int ssd1351_init(ssd1351_handle_t *handle, const ssd1351_config_t *config)
{
    /* 通用初始化序列，参数需按面板调整 */
    static const uint8_t unlock[] = { SSD1351_LOCK_UNLOCK };
    static const uint8_t access[] = { SSD1351_LOCK_ACCESS };
    static const uint8_t clock_div[] = { SSD1351_CLOCK_DIV };
    static const uint8_t mux[] = { SSD1351_MUX_128 };
    static const uint8_t start_line[] = { 0x00U };
    static const uint8_t offset[] = { 0x00U };
    static const uint8_t gpio[] = { 0x00U };
    static const uint8_t func[] = { SSD1351_FUNC_SELECT_VAL };
    static const uint8_t precharge[] = { SSD1351_PRECHARGE_PERIOD };
    static const uint8_t enhance[] = { SSD1351_ENHANCE_A, 0x00U, 0x00U };
    static const uint8_t vsl[] = { SSD1351_VSL_A, SSD1351_VSL_B, SSD1351_VSL_C };
    static const uint8_t vcomh[] = { SSD1351_VCOMH_VOLT };
    static const uint8_t master[] = { SSD1351_CONTRAST_MASTER_VAL };
    static const uint8_t abc[] = {
        SSD1351_CONTRAST_ABC_VAL, SSD1351_CONTRAST_ABC_VAL, SSD1351_CONTRAST_ABC_VAL
    };
    uint8_t remap = SSD1351_REMAP_65K;
    uint8_t precharge_volt = SSD1351_PRECHARGE_VOLT_VAL;

    if (handle == NULL || config == NULL || config->gram == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    if (config->gram_len < SSD1351_GRAM_SIZE) {
        return (int)PORT_INVALID_PARAM;
    }
    if (config->spi.handle == NULL || config->dc == 0U) {
        return (int)PORT_INVALID_PARAM;
    }

    handle->config = *config;
    handle->width = (uint8_t)SSD1351_WIDTH;
    handle->height = (uint8_t)SSD1351_HEIGHT;
    handle->initialized = false;

    if (driver_gpio_output(config->dc, PORT_GPIO_LOW) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    if (config->res != 0U) {
        (void)driver_gpio_output(config->res, PORT_GPIO_LOW);
        driver_delay_ms(SSD1351_RESET_LOW_MS);
        (void)driver_gpio_write(config->res, PORT_GPIO_HIGH);
        driver_delay_ms(SSD1351_RESET_WAIT_MS);
    }

    if (config->mirror_x) {
        remap |= 0x02U;         /* 列地址反向 */
    }
    if (config->mirror_y) {
        remap |= 0x10U;         /* 行地址反向 */
    }

    if (ssd1351_cmd_arg(handle, SSD1351_CMD_COMMAND_LOCK, unlock, sizeof(unlock)) != DRIVER_OK ||
        ssd1351_cmd_arg(handle, SSD1351_CMD_COMMAND_LOCK, access, sizeof(access)) != DRIVER_OK ||
        ssd1351_cmd(handle, SSD1351_CMD_DISPLAY_OFF) != DRIVER_OK ||
        ssd1351_cmd_arg(handle, SSD1351_CMD_SET_CLOCK, clock_div, sizeof(clock_div)) != DRIVER_OK ||
        ssd1351_cmd_arg(handle, SSD1351_CMD_SET_MUX, mux, sizeof(mux)) != DRIVER_OK ||
        ssd1351_cmd_arg(handle, SSD1351_CMD_SEG_REMAP, &remap, 1U) != DRIVER_OK ||
        ssd1351_cmd_arg(handle, SSD1351_CMD_SET_START_LINE, start_line, sizeof(start_line)) != DRIVER_OK ||
        ssd1351_cmd_arg(handle, SSD1351_CMD_SET_OFFSET, offset, sizeof(offset)) != DRIVER_OK ||
        ssd1351_cmd_arg(handle, SSD1351_CMD_SET_GPIO, gpio, sizeof(gpio)) != DRIVER_OK ||
        ssd1351_cmd_arg(handle, SSD1351_CMD_FUNC_SELECT, func, sizeof(func)) != DRIVER_OK ||
        ssd1351_cmd_arg(handle, SSD1351_CMD_SET_PRECHARGE, precharge, sizeof(precharge)) != DRIVER_OK ||
        ssd1351_cmd_arg(handle, SSD1351_CMD_DISPLAY_ENHANCE, enhance, sizeof(enhance)) != DRIVER_OK ||
        ssd1351_cmd_arg(handle, SSD1351_CMD_SET_VSL, vsl, sizeof(vsl)) != DRIVER_OK ||
        ssd1351_cmd_arg(handle, SSD1351_CMD_PRECHARGE_VOLT, &precharge_volt, 1U) != DRIVER_OK ||
        ssd1351_cmd_arg(handle, SSD1351_CMD_SET_VCOMH, vcomh, sizeof(vcomh)) != DRIVER_OK ||
        ssd1351_cmd_arg(handle, SSD1351_CMD_CONTRAST_MASTER, master, sizeof(master)) != DRIVER_OK ||
        ssd1351_cmd_arg(handle, SSD1351_CMD_CONTRAST_ABC, abc, sizeof(abc)) != DRIVER_OK ||
        ssd1351_cmd(handle, SSD1351_CMD_DISPLAY_ON) != DRIVER_OK) {
        LOG_E("ssd1351", "init sequence failed");
        return (int)PORT_IO_ERROR;
    }
    driver_delay_ms(SSD1351_DISPON_MS);

    ssd1351_gram_fill(handle, SSD1351_COLOR_BLACK);
    if (ssd1351_push(handle) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }

    handle->initialized = true;
    LOG_I("ssd1351", "init ok");
    return DRIVER_OK;
}

int ssd1351_deinit(ssd1351_handle_t *handle)
{
    if (handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    if (handle->initialized) {
        (void)ssd1351_cmd(handle, SSD1351_CMD_DISPLAY_OFF);
    }
    handle->initialized = false;
    return DRIVER_OK;
}

int ssd1351_write_cmd(ssd1351_handle_t *handle, const uint8_t *cmd, size_t len)
{
    if (handle == NULL || cmd == NULL || len == 0U || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    return ssd1351_send(handle, false, cmd, len);
}

int ssd1351_write_data(ssd1351_handle_t *handle, const uint8_t *data, size_t len)
{
    if (handle == NULL || data == NULL || len == 0U || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    return ssd1351_send(handle, true, data, len);
}

int ssd1351_fill_color(ssd1351_handle_t *handle, uint16_t color)
{
    if (handle == NULL || handle->config.gram == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    ssd1351_gram_fill(handle, color);
    return ssd1351_push(handle);
}

int ssd1351_clear(ssd1351_handle_t *handle)
{
    return ssd1351_fill_color(handle, SSD1351_COLOR_BLACK);
}

int ssd1351_draw_pixel(ssd1351_handle_t *handle, uint8_t x, uint8_t y, uint16_t color)
{
    size_t index;

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

    index = ((size_t)y * SSD1351_WIDTH + x) * 2U;
    handle->config.gram[index] = (uint8_t)(color >> 8);
    handle->config.gram[index + 1U] = (uint8_t)(color & 0xFFU);
    return DRIVER_OK;
}

int ssd1351_show(ssd1351_handle_t *handle)
{
    if (handle == NULL || handle->config.gram == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    return ssd1351_push(handle);
}

int ssd1351_set_display(ssd1351_handle_t *handle, bool on)
{
    if (handle == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    return ssd1351_cmd(handle, on ? SSD1351_CMD_DISPLAY_ON : SSD1351_CMD_DISPLAY_OFF);
}

int ssd1351_set_contrast(ssd1351_handle_t *handle, uint8_t contrast)
{
    if (handle == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    return ssd1351_cmd_arg(handle, SSD1351_CMD_CONTRAST_MASTER, &contrast, 1U);
}
