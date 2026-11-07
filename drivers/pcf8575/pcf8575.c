#include "pcf8575.h"
#include "pcf8575_register.h"
#include "log.h"

int pcf8575_init(pcf8575_handle_t *handle, port_i2c_handle_t *i2c, uint16_t address)
{
    if (handle == NULL || i2c == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : PCF8575_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->output_latch = PCF8575_ALL_HIGH;
    handle->initialized = false;

    if (handle->i2c.address > PCF8575_ADDRESS_MAX) {
        return (int)PORT_INVALID_PARAM;
    }

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("pcf8575", "probe failed at 0x%02X", handle->i2c.address);
        return (int)PORT_NOT_FOUND;
    }

    handle->initialized = true;
    /* 全部写 1，引脚置高以便作为输入使用 */
    if (pcf8575_write(handle, PCF8575_ALL_HIGH) != DRIVER_OK) {
        handle->initialized = false;
        return (int)PORT_IO_ERROR;
    }

    LOG_I("pcf8575", "init ok at 0x%02X", handle->i2c.address);
    return DRIVER_OK;
}

int pcf8575_deinit(pcf8575_handle_t *handle)
{
    if (handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int pcf8575_write(pcf8575_handle_t *handle, uint16_t value)
{
    uint8_t buf[PCF8575_FRAME_LEN];

    if (handle == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }

    /* 第一个字节为 P0（P0.0~P0.7），第二个字节为 P1 */
    buf[0] = (uint8_t)(value & 0xFFU);
    buf[1] = (uint8_t)((value >> 8) & 0xFFU);

    if (driver_i2c_write(&handle->i2c, buf, PCF8575_FRAME_LEN) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    handle->output_latch = value;
    return DRIVER_OK;
}

int pcf8575_read(pcf8575_handle_t *handle, uint16_t *value)
{
    uint8_t buf[PCF8575_FRAME_LEN] = { 0U, 0U };

    if (handle == NULL || value == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }

    if (driver_i2c_read(&handle->i2c, buf, PCF8575_FRAME_LEN) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    *value = (uint16_t)(((uint16_t)buf[1] << 8) | (uint16_t)buf[0]);
    return DRIVER_OK;
}

int pcf8575_write_pin(pcf8575_handle_t *handle, uint8_t pin, bool level)
{
    uint16_t value;

    if (handle == NULL || !handle->initialized || pin >= PCF8575_PIN_COUNT) {
        return (int)PORT_INVALID_PARAM;
    }

    value = handle->output_latch;
    if (level) {
        value = (uint16_t)(value | (uint16_t)DRIVER_BIT(pin));
    } else {
        value = (uint16_t)(value & (uint16_t)~DRIVER_BIT(pin));
    }
    return pcf8575_write(handle, value);
}

int pcf8575_read_pin(pcf8575_handle_t *handle, uint8_t pin, bool *level)
{
    uint16_t value = 0U;
    int ret;

    if (handle == NULL || level == NULL || !handle->initialized ||
        pin >= PCF8575_PIN_COUNT) {
        return (int)PORT_INVALID_PARAM;
    }

    ret = pcf8575_read(handle, &value);
    if (ret != DRIVER_OK) {
        return ret;
    }
    *level = ((value & (uint16_t)DRIVER_BIT(pin)) != 0U);
    return DRIVER_OK;
}

int pcf8575_get_latch(pcf8575_handle_t *handle, uint16_t *value)
{
    if (handle == NULL || value == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    *value = handle->output_latch;
    return DRIVER_OK;
}
