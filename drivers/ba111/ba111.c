#include "ba111.h"
#include "ba111_register.h"
#include "log.h"

int ba111_init(ba111_handle_t *handle, port_i2c_handle_t *i2c, uint16_t address)
{
    if (handle == NULL || i2c == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? (uint16_t)(address & BA111_ADDRESS_MASK)
                                          : BA111_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->initialized = false;

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("ba111", "probe failed, addr=0x%02X", handle->i2c.address);
        return (int)PORT_NOT_FOUND;
    }

    driver_delay_ms(BA111_POWER_ON_DELAY_MS);

    handle->initialized = true;
    LOG_I("ba111", "init ok");
    return DRIVER_OK;
}

int ba111_deinit(ba111_handle_t *handle)
{
    if (handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    if (!handle->initialized) {
        return (int)PORT_NOT_READY;
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int ba111_read_reg(ba111_handle_t *handle, uint8_t reg, uint8_t *data, size_t len)
{
    if (handle == NULL || data == NULL || !handle->initialized || len == 0U) {
        return (int)PORT_INVALID_PARAM;
    }
    return driver_i2c_read_reg(&handle->i2c, reg, data, len);
}

int ba111_write_reg(ba111_handle_t *handle, uint8_t reg, const uint8_t *data, size_t len)
{
    if (handle == NULL || data == NULL || !handle->initialized || len == 0U) {
        return (int)PORT_INVALID_PARAM;
    }
    return driver_i2c_write_reg(&handle->i2c, reg, data, len);
}

int ba111_read_raw(ba111_handle_t *handle, int16_t *raw)
{
    uint8_t buf[2];
    int ret;

    if (handle == NULL || raw == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    ret = ba111_read_reg(handle, BA111_REG_TEMPERATURE, buf, sizeof(buf));
    if (ret != DRIVER_OK) {
        return ret;
    }
    *raw = (int16_t)(((uint16_t)buf[0] << 8) | (uint16_t)buf[1]);
    return DRIVER_OK;
}

int ba111_read(ba111_handle_t *handle, float *celsius)
{
    int16_t raw = 0;
    int ret;

    if (celsius == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    ret = ba111_read_raw(handle, &raw);
    if (ret != DRIVER_OK) {
        return ret;
    }
    *celsius = (float)raw * BA111_LSB_CELSIUS;
    return DRIVER_OK;
}
