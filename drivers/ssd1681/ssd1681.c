#include "ssd1681.h"
#include "ssd1681_register.h"
#include "log.h"

static int ssd1681_send(ssd1681_handle_t *handle, bool is_data,
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

static int ssd1681_cmd(ssd1681_handle_t *handle, uint8_t cmd)
{
    return ssd1681_send(handle, false, &cmd, 1U);
}

static int ssd1681_cmd_arg(ssd1681_handle_t *handle, uint8_t cmd,
                           const uint8_t *arg, size_t len)
{
    if (ssd1681_cmd(handle, cmd) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    if (ssd1681_send(handle, true, arg, len) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    return DRIVER_OK;
}

/* 等待 BUSY 变低（空闲）。超时返回 PORT_TIMEOUT */
static int ssd1681_wait_busy(ssd1681_handle_t *handle, uint32_t timeout_ms)
{
    uint32_t start = driver_tick_ms();

    for (;;) {
        int level = driver_gpio_read(handle->config.busy);

        if (level < 0) {
            return (int)PORT_IO_ERROR;
        }
        if (level == 0) {
            return DRIVER_OK;
        }
        if ((driver_tick_ms() - start) >= timeout_ms) {
            LOG_E("ssd1681", "busy timeout");
            return (int)PORT_TIMEOUT;
        }
        driver_delay_ms(1U);
    }
}

static int ssd1681_reset(ssd1681_handle_t *handle)
{
    (void)driver_gpio_write(handle->config.res, PORT_GPIO_LOW);
    driver_delay_ms(SSD1681_RESET_LOW_MS);
    (void)driver_gpio_write(handle->config.res, PORT_GPIO_HIGH);
    driver_delay_ms(SSD1681_RESET_WAIT_MS);
    return ssd1681_wait_busy(handle, SSD1681_BUSY_TIMEOUT_MS);
}

/* 把显存写入 BW RAM（0x24） */
static int ssd1681_write_ram(ssd1681_handle_t *handle)
{
    uint8_t x_cnt = 0x00U;
    uint8_t y_cnt[2] = { 0x00U, 0x00U };
    uint16_t row;
    int ret = DRIVER_OK;

    if (ssd1681_cmd_arg(handle, SSD1681_CMD_SET_RAM_X_CNT, &x_cnt, 1U) != DRIVER_OK ||
        ssd1681_cmd_arg(handle, SSD1681_CMD_SET_RAM_Y_CNT, y_cnt, sizeof(y_cnt)) != DRIVER_OK ||
        ssd1681_cmd(handle, SSD1681_CMD_WRITE_RAM_BW) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }

    driver_spi_select(&handle->config.spi);
    if (driver_gpio_write(handle->config.dc, PORT_GPIO_HIGH) != DRIVER_OK) {
        driver_spi_deselect(&handle->config.spi);
        return (int)PORT_IO_ERROR;
    }
    for (row = 0U; row < SSD1681_HEIGHT; row++) {
        ret = driver_spi_write(&handle->config.spi,
                               &handle->config.gram[(size_t)row * SSD1681_WIDTH_BYTES],
                               SSD1681_WIDTH_BYTES);
        if (ret != DRIVER_OK) {
            break;
        }
    }
    driver_spi_deselect(&handle->config.spi);
    return (ret == DRIVER_OK) ? DRIVER_OK : (int)PORT_IO_ERROR;
}

int ssd1681_init(ssd1681_handle_t *handle, const ssd1681_config_t *config)
{
    /* 初始化序列为 200x200 通用流程，参数需按面板调整 */
    static const uint8_t driver_output[] = {
        SSD1681_GATE_COUNT_L, SSD1681_GATE_COUNT_H, 0x00U
    };
    static const uint8_t entry_mode[] = { SSD1681_DATA_ENTRY_MODE };
    static const uint8_t ram_x[] = { 0x00U, (uint8_t)(SSD1681_WIDTH_BYTES - 1U) };
    static const uint8_t ram_y[] = {
        SSD1681_GATE_COUNT_L, SSD1681_GATE_COUNT_H, 0x00U, 0x00U
    };
    static const uint8_t border[] = { SSD1681_BORDER_WAVEFORM };
    static const uint8_t temp[] = { SSD1681_TEMP_SENSOR_VAL };
    uint16_t x;
    uint16_t y;

    if (handle == NULL || config == NULL || config->gram == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    if (config->gram_len < SSD1681_GRAM_SIZE) {
        return (int)PORT_INVALID_PARAM;
    }
    if (config->spi.handle == NULL || config->dc == 0U ||
        config->res == 0U || config->busy == 0U) {
        return (int)PORT_INVALID_PARAM;
    }

    handle->config = *config;
    handle->width = (uint16_t)SSD1681_WIDTH;
    handle->height = (uint16_t)SSD1681_HEIGHT;
    handle->initialized = false;
    handle->dirty = false;

    if (driver_gpio_output(config->dc, PORT_GPIO_LOW) != DRIVER_OK ||
        driver_gpio_output(config->res, PORT_GPIO_HIGH) != DRIVER_OK ||
        driver_gpio_input(config->busy, PORT_GPIO_NOPULL) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }

    if (ssd1681_reset(handle) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }

    if (ssd1681_cmd(handle, SSD1681_CMD_SW_RESET) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    if (ssd1681_wait_busy(handle, SSD1681_BUSY_TIMEOUT_MS) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }

    if (ssd1681_cmd_arg(handle, SSD1681_CMD_DRIVER_OUTPUT, driver_output, sizeof(driver_output)) != DRIVER_OK ||
        ssd1681_cmd_arg(handle, SSD1681_CMD_DATA_ENTRY, entry_mode, sizeof(entry_mode)) != DRIVER_OK ||
        ssd1681_cmd_arg(handle, SSD1681_CMD_SET_RAM_X, ram_x, sizeof(ram_x)) != DRIVER_OK ||
        ssd1681_cmd_arg(handle, SSD1681_CMD_SET_RAM_Y, ram_y, sizeof(ram_y)) != DRIVER_OK ||
        ssd1681_cmd_arg(handle, SSD1681_CMD_WRITE_BORDER, border, sizeof(border)) != DRIVER_OK ||
        ssd1681_cmd_arg(handle, SSD1681_CMD_TEMP_SENSOR, temp, sizeof(temp)) != DRIVER_OK) {
        LOG_E("ssd1681", "init sequence failed");
        return (int)PORT_IO_ERROR;
    }

    /* 清显存并做一次全屏刷新，避免上电残留 */
    for (y = 0U; y < SSD1681_HEIGHT; y++) {
        for (x = 0U; x < SSD1681_WIDTH_BYTES; x++) {
            handle->config.gram[(size_t)y * SSD1681_WIDTH_BYTES + x] = 0xFFU;
        }
    }
    handle->initialized = true;
    if (ssd1681_show(handle) != DRIVER_OK) {
        handle->initialized = false;
        return (int)PORT_IO_ERROR;
    }

    LOG_I("ssd1681", "init ok (%ux%u)", (unsigned)SSD1681_WIDTH, (unsigned)SSD1681_HEIGHT);
    return DRIVER_OK;
}

int ssd1681_deinit(ssd1681_handle_t *handle)
{
    if (handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    if (handle->initialized) {
        uint8_t mode = SSD1681_DEEP_SLEEP_MODE1;

        (void)ssd1681_cmd_arg(handle, SSD1681_CMD_DEEP_SLEEP, &mode, 1U);
    }
    handle->initialized = false;
    return DRIVER_OK;
}

int ssd1681_write_cmd(ssd1681_handle_t *handle, const uint8_t *cmd, size_t len)
{
    if (handle == NULL || cmd == NULL || len == 0U) {
        return (int)PORT_INVALID_PARAM;
    }
    return ssd1681_send(handle, false, cmd, len);
}

int ssd1681_write_data(ssd1681_handle_t *handle, const uint8_t *data, size_t len)
{
    if (handle == NULL || data == NULL || len == 0U) {
        return (int)PORT_INVALID_PARAM;
    }
    return ssd1681_send(handle, true, data, len);
}

int ssd1681_fill(ssd1681_handle_t *handle, bool white)
{
    size_t i;
    uint8_t value;

    if (handle == NULL || handle->config.gram == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    value = white ? 0xFFU : 0x00U;
    for (i = 0U; i < SSD1681_GRAM_SIZE; i++) {
        handle->config.gram[i] = value;
    }
    handle->dirty = true;
    return DRIVER_OK;
}

int ssd1681_clear(ssd1681_handle_t *handle)
{
    return ssd1681_fill(handle, true);
}

int ssd1681_draw_pixel(ssd1681_handle_t *handle, uint16_t x, uint16_t y, bool black)
{
    size_t index;
    uint8_t mask;

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

    index = (size_t)y * SSD1681_WIDTH_BYTES + (size_t)(x / 8U);
    mask = (uint8_t)(0x80U >> (x % 8U));    /* MSB 为最左像素 */

    if (black) {
        handle->config.gram[index] &= (uint8_t)~mask;
    } else {
        handle->config.gram[index] |= mask;
    }
    handle->dirty = true;
    return DRIVER_OK;
}

int ssd1681_show(ssd1681_handle_t *handle)
{
    uint8_t update[2] = { SSD1681_UPDATE_SEQ_FULL, 0x00U };

    if (handle == NULL || handle->config.gram == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    if (ssd1681_wait_busy(handle, SSD1681_BUSY_TIMEOUT_MS) != DRIVER_OK) {
        return (int)PORT_TIMEOUT;
    }
    if (ssd1681_write_ram(handle) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }

    /* 显示更新流程：DISP_UPDATE2 -> MASTER_ACT -> 等待 BUSY 释放 */
    if (ssd1681_cmd_arg(handle, SSD1681_CMD_DISP_UPDATE2, update, 1U) != DRIVER_OK ||
        ssd1681_cmd(handle, SSD1681_CMD_MASTER_ACT) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    if (ssd1681_wait_busy(handle, SSD1681_BUSY_TIMEOUT_MS) != DRIVER_OK) {
        return (int)PORT_TIMEOUT;
    }
    handle->dirty = false;
    return DRIVER_OK;
}

int ssd1681_is_busy(ssd1681_handle_t *handle)
{
    if (handle == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    return driver_gpio_read(handle->config.busy);
}

