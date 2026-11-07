#include "mcp9600.h"
#include "mcp9600_register.h"
#include "log.h"

static int mcp9600_read_s16(mcp9600_handle_t *handle, uint8_t reg, float *celsius)
{
    uint8_t buf[2];

    if (driver_i2c_read_reg(&handle->i2c, reg, buf, sizeof(buf)) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }

    /* 16 位有符号，大端，1 LSB = 0.0625 ℃ */
    *celsius = (float)(int16_t)(((uint16_t)buf[0] << 8) | (uint16_t)buf[1]) *
               MCP9600_LSB_CELSIUS;
    return DRIVER_OK;
}

static int mcp9600_update_sensor_config(mcp9600_handle_t *handle, uint8_t mask, uint8_t value)
{
    uint8_t config = 0U;

    if (driver_i2c_read_reg(&handle->i2c, MCP9600_REG_SENSOR_CONFIG, &config, 1U) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    config = (uint8_t)((config & (uint8_t)~mask) | (value & mask));
    if (driver_i2c_write_reg(&handle->i2c, MCP9600_REG_SENSOR_CONFIG, &config, 1U) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    driver_delay_ms(MCP9600_CONFIG_DELAY_MS);
    return DRIVER_OK;
}

int mcp9600_init(mcp9600_handle_t *handle, iot_i2c_t *i2c, uint16_t address)
{
    uint8_t id = 0U;

    if (handle == NULL || i2c == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? (uint16_t)(address & MCP9600_ADDRESS_MASK)
                                          : MCP9600_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->initialized = false;

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("mcp9600", "probe failed, addr=0x%02X", handle->i2c.address);
        return (int)IOT_NOT_FOUND;
    }

    driver_delay_ms(MCP9600_POWER_ON_DELAY_MS);

    handle->initialized = true;

    /* 器件 ID 校验失败只告警：部分兼容器件 ID 不同 */
    if (driver_i2c_read_reg(&handle->i2c, MCP9600_REG_DEVICE_ID, &id, 1U) == DRIVER_OK &&
        id != MCP9600_DEVICE_ID_VALUE) {
        LOG_W("mcp9600", "unexpected device id 0x%02X", (unsigned int)id);
    }

    LOG_I("mcp9600", "init ok");
    return DRIVER_OK;
}

int mcp9600_deinit(mcp9600_handle_t *handle)
{
    if (handle == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    if (!handle->initialized) {
        return (int)IOT_NOT_READY;
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int mcp9600_read(mcp9600_handle_t *handle, float *celsius)
{
    if (handle == NULL || celsius == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    return mcp9600_read_s16(handle, MCP9600_REG_HOT_JUNCTION, celsius);
}

int mcp9600_read_cold_junction(mcp9600_handle_t *handle, float *celsius)
{
    if (handle == NULL || celsius == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    return mcp9600_read_s16(handle, MCP9600_REG_COLD_JUNCTION, celsius);
}

int mcp9600_read_delta(mcp9600_handle_t *handle, float *celsius)
{
    if (handle == NULL || celsius == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    return mcp9600_read_s16(handle, MCP9600_REG_JUNCTION_DELTA, celsius);
}

int mcp9600_set_thermocouple_type(mcp9600_handle_t *handle, uint8_t type)
{
    if (handle == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    if (type > MCP9600_TC_TYPE_S) {
        return (int)IOT_INVALID_PARAM;
    }
    return mcp9600_update_sensor_config(handle, MCP9600_TC_MASK,
                                        (uint8_t)(type << MCP9600_TC_SHIFT));
}

int mcp9600_set_filter(mcp9600_handle_t *handle, uint8_t coefficient)
{
    if (handle == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    if (coefficient > MCP9600_FILTER_MASK) {
        return (int)IOT_INVALID_PARAM;
    }
    return mcp9600_update_sensor_config(handle, MCP9600_FILTER_MASK, coefficient);
}

int mcp9600_get_device_id(mcp9600_handle_t *handle, uint8_t *id)
{
    if (handle == NULL || id == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    return driver_i2c_read_reg(&handle->i2c, MCP9600_REG_DEVICE_ID, id, 1U);
}

int mcp9600_read_reg(mcp9600_handle_t *handle, uint8_t reg, uint8_t *data, size_t len)
{
    if (handle == NULL || data == NULL || !handle->initialized || len == 0U) {
        return (int)IOT_INVALID_PARAM;
    }
    return driver_i2c_read_reg(&handle->i2c, reg, data, len);
}

int mcp9600_write_reg(mcp9600_handle_t *handle, uint8_t reg, const uint8_t *data, size_t len)
{
    if (handle == NULL || data == NULL || !handle->initialized || len == 0U) {
        return (int)IOT_INVALID_PARAM;
    }
    return driver_i2c_write_reg(&handle->i2c, reg, data, len);
}
