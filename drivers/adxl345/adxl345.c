#include "adxl345.h"
#include "adxl345_register.h"
#include "log.h"

int adxl345_init(adxl345_handle_t *handle, port_i2c_handle_t *i2c,
                 uint16_t address, adxl345_range_t range)
{
    uint8_t devid = 0U;
    uint8_t value;

    if (handle == NULL || i2c == NULL || (uint8_t)range > ADXL345_RANGE_16G) {
        return (int)PORT_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : ADXL345_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->range = range;
    handle->initialized = false;

    if (driver_i2c_read_reg(&handle->i2c, ADXL345_REG_DEVID, &devid, 1U) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    if (devid != ADXL345_DEVID_VALUE) {
        LOG_E("adxl345", "devid mismatch: 0x%02X", devid);
        return (int)PORT_NOT_FOUND;
    }

    /* 全分辨率 + 量程 */
    value = (uint8_t)(ADXL345_FORMAT_FULL_RES | (uint8_t)range);
    if (driver_i2c_write_reg(&handle->i2c, ADXL345_REG_DATA_FORMAT, &value, 1U) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }

    /* 100Hz 输出率 */
    value = ADXL345_BW_RATE_100HZ;
    if (driver_i2c_write_reg(&handle->i2c, ADXL345_REG_BW_RATE, &value, 1U) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }

    if (adxl345_set_measure(handle, true) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }

    handle->initialized = true;
    LOG_I("adxl345", "init ok at 0x%02X, range=%d", handle->i2c.address, (int)range);
    return DRIVER_OK;
}

int adxl345_deinit(adxl345_handle_t *handle)
{
    if (handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    if (handle->initialized) {
        (void)adxl345_set_measure(handle, false);
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int adxl345_set_measure(adxl345_handle_t *handle, bool enable)
{
    uint8_t value = enable ? ADXL345_POWER_MEASURE : ADXL345_POWER_STANDBY;

    if (handle == NULL || handle->i2c.handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    return driver_i2c_write_reg(&handle->i2c, ADXL345_REG_POWER_CTL, &value, 1U);
}

int adxl345_read_raw(adxl345_handle_t *handle, int16_t xyz[3])
{
    uint8_t d[6];

    if (handle == NULL || xyz == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }

    if (driver_i2c_read_reg(&handle->i2c, ADXL345_REG_DATAX0, d, sizeof(d)) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }

    xyz[0] = (int16_t)((uint16_t)d[0] | ((uint16_t)d[1] << 8));
    xyz[1] = (int16_t)((uint16_t)d[2] | ((uint16_t)d[3] << 8));
    xyz[2] = (int16_t)((uint16_t)d[4] | ((uint16_t)d[5] << 8));
    return DRIVER_OK;
}

int adxl345_read(adxl345_handle_t *handle, adxl345_accel_t *accel)
{
    int16_t xyz[3];
    int ret;

    if (accel == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    ret = adxl345_read_raw(handle, xyz);
    if (ret != DRIVER_OK) {
        return ret;
    }

    accel->x = (float)xyz[0] * ADXL345_SCALE_G_PER_LSB;
    accel->y = (float)xyz[1] * ADXL345_SCALE_G_PER_LSB;
    accel->z = (float)xyz[2] * ADXL345_SCALE_G_PER_LSB;
    return DRIVER_OK;
}
