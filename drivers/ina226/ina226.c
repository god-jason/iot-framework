#include "ina226.h"
#include "ina226_register.h"
#include "log.h"

#define INA226_TAG "ina226"

static int ina226_read_reg16(ina226_handle_t *handle, uint8_t reg, uint16_t *value)
{
    uint8_t d[2];

    if (driver_i2c_read_reg(&handle->i2c, reg, d, sizeof(d)) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    *value = (uint16_t)(((uint16_t)d[0] << 8) | (uint16_t)d[1]);
    return DRIVER_OK;
}

static int ina226_write_reg16(ina226_handle_t *handle, uint8_t reg, uint16_t value)
{
    uint8_t d[2];

    d[0] = (uint8_t)(value >> 8);
    d[1] = (uint8_t)(value & 0xFFU);
    return driver_i2c_write_reg(&handle->i2c, reg, d, sizeof(d));
}

/* 按分流电阻与满量程电流计算 CAL = 0.00512 / (current_lsb * Rshunt) 并写入 */
static int ina226_apply_calibration(ina226_handle_t *handle, float shunt_ohm, float max_current_a)
{
    float current_lsb;
    float cal;

    if (shunt_ohm <= 0.0f || max_current_a <= 0.0f) {
        return (int)PORT_INVALID_PARAM;
    }

    current_lsb = max_current_a / INA226_CURRENT_LSB_DIVISOR;
    cal = INA226_CAL_FACTOR / (current_lsb * shunt_ohm);
    if (cal < 1.0f) {
        cal = 1.0f;
    }
    if (cal > (float)INA226_CAL_MAX) {
        cal = (float)INA226_CAL_MAX;
    }

    handle->calibration = (uint16_t)(cal + 0.5f);
    if (ina226_write_reg16(handle, INA226_REG_CALIBRATION, handle->calibration) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }

    handle->shunt_ohm = shunt_ohm;
    handle->max_current_a = max_current_a;
    handle->current_lsb_a = current_lsb;
    handle->power_lsb_w = current_lsb * INA226_POWER_LSB_FACTOR;
    return DRIVER_OK;
}

int ina226_init(ina226_handle_t *handle, port_i2c_handle_t *i2c, uint16_t address)
{
    uint16_t id = 0U;
    uint16_t manuf = 0U;

    if (handle == NULL || i2c == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : INA226_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->shunt_ohm = INA226_DEFAULT_SHUNT_OHM;
    handle->max_current_a = INA226_DEFAULT_MAX_CURRENT_A;
    handle->current_lsb_a = INA226_DEFAULT_MAX_CURRENT_A / INA226_CURRENT_LSB_DIVISOR;
    handle->power_lsb_w = handle->current_lsb_a * INA226_POWER_LSB_FACTOR;
    handle->calibration = 0U;
    handle->initialized = false;

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E(INA226_TAG, "probe failed at 0x%02X", handle->i2c.address);
        return (int)PORT_NOT_FOUND;
    }
    if (ina226_read_reg16(handle, INA226_REG_DIE_ID, &id) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    if (id != INA226_DIE_ID_VALUE) {
        LOG_E(INA226_TAG, "die id mismatch: 0x%04X", (unsigned)id);
        return (int)PORT_NOT_FOUND;
    }
    if (ina226_read_reg16(handle, INA226_REG_MANUFACTURER_ID, &manuf) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    if (manuf != INA226_MANUFACTURER_ID_VALUE) {
        LOG_E(INA226_TAG, "manufacturer id mismatch: 0x%04X", (unsigned)manuf);
        return (int)PORT_NOT_FOUND;
    }

    if (ina226_write_reg16(handle, INA226_REG_CONFIG, INA226_CONFIG_DEFAULT) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    if (ina226_apply_calibration(handle, INA226_DEFAULT_SHUNT_OHM, INA226_DEFAULT_MAX_CURRENT_A)
        != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    driver_delay_ms(2U);

    handle->initialized = true;
    LOG_I(INA226_TAG, "init ok at 0x%02X, cal=%u",
          handle->i2c.address, (unsigned)handle->calibration);
    return DRIVER_OK;
}

int ina226_deinit(ina226_handle_t *handle)
{
    if (handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    if (handle->initialized && handle->i2c.handle != NULL) {
        /* MODE = 0：关机，停止转换 */
        (void)ina226_write_reg16(handle, INA226_REG_CONFIG, 0x0000U);
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int ina226_set_calibration(ina226_handle_t *handle, float shunt_ohm, float max_current_a)
{
    if (handle == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    return ina226_apply_calibration(handle, shunt_ohm, max_current_a);
}

int ina226_read_raw(ina226_handle_t *handle, int16_t *shunt_raw, uint16_t *bus_raw,
                    int16_t *current_raw, uint16_t *power_raw)
{
    uint8_t d[8];

    if (handle == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    if (shunt_raw == NULL && bus_raw == NULL && current_raw == NULL && power_raw == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    /* 0x01~0x04 连续读取：分流 / 总线 / 功率 / 电流，均为大端 */
    if (driver_i2c_read_reg(&handle->i2c, INA226_REG_SHUNT_VOLTAGE, d, sizeof(d)) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }

    if (shunt_raw != NULL) {
        *shunt_raw = (int16_t)(((uint16_t)d[0] << 8) | (uint16_t)d[1]);
    }
    if (bus_raw != NULL) {
        *bus_raw = (uint16_t)(((uint16_t)d[2] << 8) | (uint16_t)d[3]);
    }
    if (power_raw != NULL) {
        *power_raw = (uint16_t)(((uint16_t)d[4] << 8) | (uint16_t)d[5]);
    }
    if (current_raw != NULL) {
        *current_raw = (int16_t)(((uint16_t)d[6] << 8) | (uint16_t)d[7]);
    }
    return DRIVER_OK;
}

int ina226_read_bus_voltage(ina226_handle_t *handle, float *voltage)
{
    uint16_t raw = 0U;

    if (handle == NULL || voltage == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    if (ina226_read_reg16(handle, INA226_REG_BUS_VOLTAGE, &raw) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }

    /* 总线电压：16 位全部有效，1 LSB = 1.25 mV，不做移位 */
    *voltage = (float)raw * INA226_BUS_LSB_V;
    return DRIVER_OK;
}

int ina226_read_shunt_voltage(ina226_handle_t *handle, float *voltage)
{
    uint16_t raw = 0U;

    if (handle == NULL || voltage == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    if (ina226_read_reg16(handle, INA226_REG_SHUNT_VOLTAGE, &raw) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }

    *voltage = (float)(int16_t)raw * INA226_SHUNT_LSB_V;
    return DRIVER_OK;
}

int ina226_read_current(ina226_handle_t *handle, float *current)
{
    uint16_t raw = 0U;

    if (handle == NULL || current == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    if (ina226_read_reg16(handle, INA226_REG_CURRENT, &raw) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }

    *current = (float)(int16_t)raw * handle->current_lsb_a;
    return DRIVER_OK;
}

int ina226_read_power(ina226_handle_t *handle, float *power)
{
    uint16_t raw = 0U;

    if (handle == NULL || power == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    if (ina226_read_reg16(handle, INA226_REG_POWER, &raw) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }

    *power = (float)raw * handle->power_lsb_w;
    return DRIVER_OK;
}

int ina226_read(ina226_handle_t *handle, ina226_data_t *data)
{
    int16_t shunt_raw = 0;
    int16_t current_raw = 0;
    uint16_t bus_raw = 0U;
    uint16_t power_raw = 0U;
    int ret;

    if (handle == NULL || data == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }

    ret = ina226_read_raw(handle, &shunt_raw, &bus_raw, &current_raw, &power_raw);
    if (ret != DRIVER_OK) {
        return ret;
    }

    data->bus_voltage = (float)bus_raw * INA226_BUS_LSB_V;
    data->shunt_voltage = (float)shunt_raw * INA226_SHUNT_LSB_V;
    data->current = (float)current_raw * handle->current_lsb_a;
    data->power = (float)power_raw * handle->power_lsb_w;
    return DRIVER_OK;
}
