#include "max30205.h"
#include "max30205_register.h"
#include "log.h"

static int max30205_to_raw(float celsius, int16_t *raw)
{
    float scaled = celsius / MAX30205_LSB_CELSIUS;

    /* 限幅在 int16 范围，避免浮点转整型溢出 */
    if (scaled > 32767.0f) {
        return (int)PORT_INVALID_PARAM;
    }
    if (scaled < -32768.0f) {
        return (int)PORT_INVALID_PARAM;
    }
    *raw = (int16_t)scaled;
    return DRIVER_OK;
}

int max30205_init(max30205_handle_t *handle, port_i2c_handle_t *i2c, uint16_t address)
{
    if (handle == NULL || i2c == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? (uint16_t)(address & MAX30205_ADDRESS_MASK)
                                          : MAX30205_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->initialized = false;

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("max30205", "probe failed, addr=0x%02X", handle->i2c.address);
        return (int)PORT_NOT_FOUND;
    }

    /* 上电后等待一次完整转换，保证首个读数有效 */
    driver_delay_ms(MAX30205_CONV_DELAY_MS);

    handle->initialized = true;
    LOG_I("max30205", "init ok");
    return DRIVER_OK;
}

int max30205_deinit(max30205_handle_t *handle)
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

int max30205_read_raw(max30205_handle_t *handle, int16_t *raw)
{
    uint8_t buf[2];

    if (handle == NULL || raw == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }

    if (driver_i2c_read_reg(&handle->i2c, MAX30205_REG_TEMPERATURE, buf, sizeof(buf)) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    *raw = (int16_t)(((uint16_t)buf[0] << 8) | (uint16_t)buf[1]);
    return DRIVER_OK;
}

int max30205_read(max30205_handle_t *handle, float *celsius)
{
    int16_t raw = 0;
    int ret;

    if (celsius == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    ret = max30205_read_raw(handle, &raw);
    if (ret != DRIVER_OK) {
        return ret;
    }
    *celsius = (float)raw * MAX30205_LSB_CELSIUS;
    return DRIVER_OK;
}

int max30205_get_config(max30205_handle_t *handle, uint16_t *config)
{
    uint8_t buf[2];

    if (handle == NULL || config == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    if (driver_i2c_read_reg(&handle->i2c, MAX30205_REG_CONFIG, buf, sizeof(buf)) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    *config = (uint16_t)(((uint16_t)buf[0] << 8) | (uint16_t)buf[1]);
    return DRIVER_OK;
}

int max30205_set_config(max30205_handle_t *handle, uint16_t config)
{
    uint8_t buf[2];

    if (handle == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    buf[0] = (uint8_t)(config >> 8);
    buf[1] = (uint8_t)(config & 0xFFU);
    if (driver_i2c_write_reg(&handle->i2c, MAX30205_REG_CONFIG, buf, sizeof(buf)) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    /* 从关断恢复需要一次转换时间 */
    driver_delay_ms(MAX30205_CONV_DELAY_MS);
    return DRIVER_OK;
}

static int max30205_write_threshold(max30205_handle_t *handle, uint8_t reg, float celsius)
{
    int16_t raw = 0;
    uint8_t buf[2];
    int ret;

    if (handle == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    ret = max30205_to_raw(celsius, &raw);
    if (ret != DRIVER_OK) {
        return ret;
    }
    buf[0] = (uint8_t)((uint16_t)raw >> 8);
    buf[1] = (uint8_t)((uint16_t)raw & 0xFFU);
    if (driver_i2c_write_reg(&handle->i2c, reg, buf, sizeof(buf)) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    return DRIVER_OK;
}

int max30205_set_overtemperature(max30205_handle_t *handle, float celsius)
{
    return max30205_write_threshold(handle, MAX30205_REG_TOS, celsius);
}

int max30205_set_hysteresis(max30205_handle_t *handle, float celsius)
{
    return max30205_write_threshold(handle, MAX30205_REG_THYST, celsius);
}
