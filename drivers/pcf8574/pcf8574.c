#include "pcf8574.h"
#include "log.h"

int pcf8574_init(pcf8574_handle_t *handle, iot_i2c_t *i2c, uint16_t address)
{
    if (handle == NULL || i2c == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : PCF8574_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->output_latch = 0xFFU;   /* 全部置高，便于作为输入 */
    handle->initialized = false;

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("pcf8574", "probe failed at 0x%02X", handle->i2c.address);
        return (int)IOT_NOT_FOUND;
    }

    if (driver_i2c_write(&handle->i2c, &handle->output_latch, 1U) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }

    handle->initialized = true;
    LOG_I("pcf8574", "init ok at 0x%02X", handle->i2c.address);
    return DRIVER_OK;
}

int pcf8574_deinit(pcf8574_handle_t *handle)
{
    if (handle == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int pcf8574_write(pcf8574_handle_t *handle, uint8_t value)
{
    int ret;

    if (handle == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }

    ret = driver_i2c_write(&handle->i2c, &value, 1U);
    if (ret == DRIVER_OK) {
        handle->output_latch = value;
    }
    return ret;
}

int pcf8574_read(pcf8574_handle_t *handle, uint8_t *value)
{
    if (handle == NULL || value == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    return driver_i2c_read(&handle->i2c, value, 1U);
}

int pcf8574_write_pin(pcf8574_handle_t *handle, uint8_t pin, bool level)
{
    uint8_t value;

    if (pin > 7U) {
        return (int)IOT_INVALID_PARAM;
    }

    value = handle->output_latch;
    if (level) {
        value = (uint8_t)(value | DRIVER_BIT(pin));
    } else {
        value = (uint8_t)(value & (uint8_t)~DRIVER_BIT(pin));
    }
    return pcf8574_write(handle, value);
}

int pcf8574_read_pin(pcf8574_handle_t *handle, uint8_t pin, bool *level)
{
    uint8_t value = 0U;
    int ret;

    if (level == NULL || pin > 7U) {
        return (int)IOT_INVALID_PARAM;
    }

    ret = pcf8574_read(handle, &value);
    if (ret != DRIVER_OK) {
        return ret;
    }
    *level = ((value & DRIVER_BIT(pin)) != 0U);
    return DRIVER_OK;
}
