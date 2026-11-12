#include "max7219.h"
#include "max7219_register.h"
#include "log.h"

static int max7219_send_frame(max7219_handle_t *handle, const uint8_t *frame, size_t len)
{
    int ret;

    hal_spi_dev_select(&handle->spi);
    ret = hal_spi_dev_write(&handle->spi, frame, len);
    hal_spi_dev_deselect(&handle->spi);
    return ret;
}

int max7219_write_index(max7219_handle_t *handle, uint8_t index,
                        uint8_t reg, uint8_t data)
{
    uint8_t frame[MAX7219_DIGIT_COUNT * 2U];
    size_t len;
    uint8_t i;

    if (handle == NULL || !handle->initialized || index >= handle->chain_length) {
        return (int)HAL_INVALID_PARAM;
    }

    len = (size_t)handle->chain_length * 2U;
    for (i = 0U; i < handle->chain_length; i++) {
        uint8_t target = (uint8_t)(handle->chain_length - 1U - i);

        if (target == index) {
            frame[i * 2U] = reg;
            frame[i * 2U + 1U] = data;
        } else {
            frame[i * 2U] = MAX7219_REG_NOOP;
            frame[i * 2U + 1U] = 0x00U;
        }
    }
    return max7219_send_frame(handle, frame, len);
}

int max7219_write(max7219_handle_t *handle, uint8_t reg, uint8_t data)
{
    uint8_t i;
    int ret;

    if (handle == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }

    for (i = 0U; i < handle->chain_length; i++) {
        ret = max7219_write_index(handle, i, reg, data);
        if (ret != HAL_OK) {
            return ret;
        }
    }
    return HAL_OK;
}

int max7219_init(max7219_handle_t *handle, hal_spi_t *spi, hal_pin_t cs,
                 uint8_t chain_length)
{
    if (handle == NULL || spi == NULL || chain_length == 0U ||
        chain_length > MAX7219_DIGIT_COUNT) {
        return (int)HAL_INVALID_PARAM;
    }

    handle->spi.handle = spi;
    handle->spi.cs = cs;
    handle->spi.timeout_ms = HAL_SPI_TIMEOUT_DEFAULT;
    handle->chain_length = chain_length;
    handle->initialized = false;

    if (cs != 0U) {
        if (hal_gpio_config_output(cs, HAL_GPIO_HIGH) != HAL_OK) {
            return (int)HAL_IO_ERROR;
        }
    }

    handle->initialized = true;

    if (max7219_set_shutdown(handle, false) != HAL_OK ||
        max7219_set_decode_mode(handle, MAX7219_DECODE_NONE) != HAL_OK ||
        max7219_set_intensity(handle, MAX7219_INTENSITY_DEFAULT) != HAL_OK ||
        max7219_set_scan_limit(handle, MAX7219_SCAN_ALL) != HAL_OK ||
        max7219_display_test(handle, false) != HAL_OK ||
        max7219_clear(handle) != HAL_OK) {
        handle->initialized = false;
        LOG_E("max7219", "init sequence failed");
        return (int)HAL_IO_ERROR;
    }

    LOG_I("max7219", "init ok, chain=%u", (unsigned)chain_length);
    return HAL_OK;
}

int max7219_deinit(max7219_handle_t *handle)
{
    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    if (handle->initialized) {
        (void)max7219_set_shutdown(handle, true);
    }
    handle->initialized = false;
    return HAL_OK;
}

int max7219_set_intensity(max7219_handle_t *handle, uint8_t intensity)
{
    if (intensity > MAX7219_INTENSITY_MAX) {
        intensity = MAX7219_INTENSITY_MAX;
    }
    return max7219_write(handle, MAX7219_REG_INTENSITY, intensity);
}

int max7219_set_decode_mode(max7219_handle_t *handle, uint8_t mode)
{
    return max7219_write(handle, MAX7219_REG_DECODE_MODE, mode);
}

int max7219_set_scan_limit(max7219_handle_t *handle, uint8_t limit)
{
    if (limit > MAX7219_SCAN_ALL) {
        limit = MAX7219_SCAN_ALL;
    }
    return max7219_write(handle, MAX7219_REG_SCAN_LIMIT, limit);
}

int max7219_set_shutdown(max7219_handle_t *handle, bool shutdown)
{
    return max7219_write(handle, MAX7219_REG_SHUTDOWN,
                         shutdown ? MAX7219_SHUTDOWN_MODE : MAX7219_NORMAL_OPERATION);
}

int max7219_display_test(max7219_handle_t *handle, bool enable)
{
    return max7219_write(handle, MAX7219_REG_DISPLAY_TEST,
                         enable ? 0x01U : MAX7219_TEST_OFF);
}

int max7219_set_digit(max7219_handle_t *handle, uint8_t digit, uint8_t value)
{
    if (digit >= MAX7219_DIGIT_COUNT) {
        return (int)HAL_INVALID_PARAM;
    }
    return max7219_write(handle, (uint8_t)(MAX7219_REG_DIGIT0 + digit), value);
}

int max7219_clear(max7219_handle_t *handle)
{
    uint8_t digit;
    int ret;

    if (handle == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    for (digit = 0U; digit < MAX7219_DIGIT_COUNT; digit++) {
        ret = max7219_set_digit(handle, digit, 0x00U);
        if (ret != HAL_OK) {
            return ret;
        }
    }
    return HAL_OK;
}
