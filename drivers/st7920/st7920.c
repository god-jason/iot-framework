#include "st7920.h"
#include "st7920_register.h"
#include "log.h"

/* 单帧缓存：2 条命令 + 16 组（2 个数据字节） */
#define ST7920_ROW_FRAME_MAX  (2U * 3U + 32U * 3U)

static int st7920_send(st7920_handle_t *handle, const uint8_t *buf, size_t len)
{
    int ret;

    driver_spi_select(&handle->config.spi);
    ret = driver_spi_write(&handle->config.spi, buf, len);
    driver_spi_deselect(&handle->config.spi);
    return ret;
}

/* 写一个字节：帧头 + 高半字节 + 低半字节 */
static int st7920_write_byte(st7920_handle_t *handle, bool is_data, uint8_t value)
{
    uint8_t buf[3];

    buf[0] = is_data ? ST7920_SYNC_DATA : ST7920_SYNC_CMD;
    buf[1] = (uint8_t)(value & 0xF0U);
    buf[2] = (uint8_t)((value << 4) & 0xF0U);
    return st7920_send(handle, buf, sizeof(buf));
}

static void st7920_frame_byte(uint8_t *buf, size_t *pos, bool is_data, uint8_t value)
{
    buf[(*pos)++] = is_data ? ST7920_SYNC_DATA : ST7920_SYNC_CMD;
    buf[(*pos)++] = (uint8_t)(value & 0xF0U);
    buf[(*pos)++] = (uint8_t)((value << 4) & 0xF0U);
}

/* 写一整行 GDRAM：垂直地址 v 与 v+32 共 16 个单元 */
static int st7920_write_row(st7920_handle_t *handle, uint8_t v)
{
    uint8_t buf[ST7920_ROW_FRAME_MAX];
    size_t pos = 0U;
    uint8_t h;

    st7920_frame_byte(buf, &pos, false, (uint8_t)(ST7920_CMD_SET_GDRAM_ADDR | (v & 0x1FU)));
    st7920_frame_byte(buf, &pos, false, ST7920_CMD_SET_GDRAM_ADDR);

    for (h = 0U; h < ST7920_WIDTH_BYTES; h++) {
        uint8_t upper = handle->config.gram[(size_t)v * ST7920_WIDTH_BYTES + h];
        uint8_t lower = handle->config.gram[(size_t)(v + 32U) * ST7920_WIDTH_BYTES + h];

        st7920_frame_byte(buf, &pos, true, upper);
        st7920_frame_byte(buf, &pos, true, lower);
    }
    return st7920_send(handle, buf, pos);
}

/* 整屏推送 */
static int st7920_push(st7920_handle_t *handle)
{
    uint8_t v;

    for (v = 0U; v < 32U; v++) {
        if (st7920_write_row(handle, v) != DRIVER_OK) {
            return (int)HAL_IO_ERROR;
        }
    }
    return DRIVER_OK;
}

int st7920_init(st7920_handle_t *handle, const st7920_config_t *config)
{
    size_t i;

    if (handle == NULL || config == NULL || config->gram == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    if (config->gram_len < ST7920_GRAM_SIZE) {
        return (int)HAL_INVALID_PARAM;
    }
    if (config->spi.handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    handle->config = *config;
    handle->width = (uint8_t)ST7920_WIDTH;
    handle->height = (uint8_t)ST7920_HEIGHT;
    handle->initialized = false;

    if (config->res != 0U) {
        (void)driver_gpio_output(config->res, HAL_GPIO_LOW);
        driver_delay_ms(ST7920_RESET_WAIT_MS);
        (void)driver_gpio_write(config->res, HAL_GPIO_HIGH);
    }
    driver_delay_ms(ST7920_POWER_ON_MS);

    /* 上电后连发两次 FUNCTION_BASIC，确保接口模式同步 */
    if (st7920_write_byte(handle, false, ST7920_CMD_FUNCTION_BASIC) != DRIVER_OK ||
        st7920_write_byte(handle, false, ST7920_CMD_FUNCTION_BASIC) != DRIVER_OK) {
        LOG_E("st7920", "sync sequence failed");
        return (int)HAL_IO_ERROR;
    }
    driver_delay_ms(ST7920_RESET_WAIT_MS);

    if (st7920_write_byte(handle, false, ST7920_CMD_DISPLAY_ON) != DRIVER_OK ||
        st7920_write_byte(handle, false, ST7920_CMD_CLEAR) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    driver_delay_ms(ST7920_CLEAR_MS);

    if (st7920_write_byte(handle, false, ST7920_CMD_ENTRY_MODE) != DRIVER_OK ||
        st7920_write_byte(handle, false, ST7920_CMD_FUNCTION_EXT) != DRIVER_OK ||
        st7920_write_byte(handle, false, ST7920_CMD_GRAPHIC_ON) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }

    for (i = 0U; i < ST7920_GRAM_SIZE; i++) {
        handle->config.gram[i] = 0x00U;
    }
    if (st7920_push(handle) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }

    handle->initialized = true;
    LOG_I("st7920", "init ok (serial mode)");
    return DRIVER_OK;
}

int st7920_deinit(st7920_handle_t *handle)
{
    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    if (handle->initialized) {
        (void)st7920_set_display(handle, false);
        (void)st7920_write_byte(handle, false, ST7920_CMD_FUNCTION_BASIC);
    }
    handle->initialized = false;
    return DRIVER_OK;
}

int st7920_write_cmd(st7920_handle_t *handle, uint8_t cmd)
{
    if (handle == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    return st7920_write_byte(handle, false, cmd);
}

int st7920_write_data(st7920_handle_t *handle, uint8_t data)
{
    if (handle == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    return st7920_write_byte(handle, true, data);
}

int st7920_fill(st7920_handle_t *handle, uint8_t pattern)
{
    size_t i;

    if (handle == NULL || handle->config.gram == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    for (i = 0U; i < ST7920_GRAM_SIZE; i++) {
        handle->config.gram[i] = pattern;
    }
    return st7920_push(handle);
}

int st7920_clear(st7920_handle_t *handle)
{
    return st7920_fill(handle, 0x00U);
}

int st7920_draw_pixel(st7920_handle_t *handle, uint8_t x, uint8_t y, bool on)
{
    size_t index;
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

    index = (size_t)y * ST7920_WIDTH_BYTES + (size_t)(x / 8U);
    mask = (uint8_t)DRIVER_BIT(x % 8U);     /* LSB 为最左像素 */

    if (on) {
        handle->config.gram[index] |= mask;
    } else {
        handle->config.gram[index] &= (uint8_t)~mask;
    }
    return DRIVER_OK;
}

int st7920_show(st7920_handle_t *handle)
{
    if (handle == NULL || handle->config.gram == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    return st7920_push(handle);
}

int st7920_set_display(st7920_handle_t *handle, bool on)
{
    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    return st7920_write_byte(handle, false,
                             on ? ST7920_CMD_DISPLAY_ON : ST7920_CMD_DISPLAY_OFF);
}
