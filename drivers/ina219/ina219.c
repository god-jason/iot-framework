#include "ina219.h"
#include "ina219_register.h"
#include "log.h"

static int ina219_read_reg16(ina219_handle_t *handle, uint8_t reg, uint16_t *value)
{
    uint8_t d[2];

    if (driver_i2c_read_reg(&handle->i2c, reg, d, sizeof(d)) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    *value = (uint16_t)(((uint16_t)d[0] << 8) | d[1]);
    return DRIVER_OK;
}

static int ina219_write_reg16(ina219_handle_t *handle, uint8_t reg, uint16_t value)
{
    uint8_t d[2];

    d[0] = (uint8_t)(value >> 8);
    d[1] = (uint8_t)(value & 0xFFU);
    return driver_i2c_write_reg(&handle->i2c, reg, d, sizeof(d));
}

int ina219_init(ina219_handle_t *handle, hal_i2c_t *i2c, uint16_t address,
                float shunt_ohm, float max_current_a)
{
    if (handle == NULL || i2c == NULL || shunt_ohm <= 0.0f || max_current_a <= 0.0f) {
        return (int)HAL_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : INA219_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->shunt_ohm = shunt_ohm;
    handle->current_lsb_a = max_current_a / 32768.0f;
    handle->power_lsb_w = handle->current_lsb_a * INA219_POWER_LSB_FACTOR;
    handle->initialized = false;

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("ina219", "probe failed at 0x%02X", handle->i2c.address);
        return (int)HAL_NOT_FOUND;
    }

    if (ina219_write_reg16(handle, INA219_REG_CONFIG,
                           INA219_CONFIG_16V_320MV_12BIT_CONT) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }

    {
        float cal = INA219_CAL_FACTOR / (handle->current_lsb_a * handle->shunt_ohm);

        if (cal < 1.0f) {
            cal = 1.0f;
        }
        if (cal > 65535.0f) {
            cal = 65535.0f;
        }
        handle->calibration = (uint16_t)cal;
    }

    if (ina219_write_reg16(handle, INA219_REG_CALIBRATION, handle->calibration) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }

    handle->initialized = true;
    LOG_I("ina219", "init ok, cal=%u", (unsigned)handle->calibration);
    return DRIVER_OK;
}

int ina219_deinit(ina219_handle_t *handle)
{
    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int ina219_read_bus_voltage(ina219_handle_t *handle, float *voltage)
{
    uint16_t raw = 0U;

    if (handle == NULL || voltage == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    if (ina219_read_reg16(handle, INA219_REG_BUS_VOLTAGE, &raw) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }

    /* 总线电压为 bit15:3，LSB 4mV */
    *voltage = (float)(raw & INA219_BUS_VOLTAGE_MASK) * INA219_BUS_LSB_V / 8.0f;
    return DRIVER_OK;
}

int ina219_read_current(ina219_handle_t *handle, float *current)
{
    uint16_t raw = 0U;

    if (handle == NULL || current == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    if (ina219_read_reg16(handle, INA219_REG_CURRENT, &raw) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    *current = (float)(int16_t)raw * handle->current_lsb_a;
    return DRIVER_OK;
}

int ina219_read_power(ina219_handle_t *handle, float *power)
{
    uint16_t raw = 0U;

    if (handle == NULL || power == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    if (ina219_read_reg16(handle, INA219_REG_POWER, &raw) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    *power = (float)raw * handle->power_lsb_w;
    return DRIVER_OK;
}

int ina219_read(ina219_handle_t *handle, ina219_data_t *data)
{
    int ret;

    if (data == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    ret = ina219_read_bus_voltage(handle, &data->bus_voltage);
    if (ret != DRIVER_OK) {
        return ret;
    }
    ret = ina219_read_current(handle, &data->current);
    if (ret != DRIVER_OK) {
        return ret;
    }
    return ina219_read_power(handle, &data->power);
}
