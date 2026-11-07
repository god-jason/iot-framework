#include "bh1750.h"
#include "bh1750_register.h"
#include "log.h"

static uint8_t bh1750_mode_command(bh1750_mode_t mode)
{
    switch (mode) {
    case BH1750_MODE_CONT_HIGH:
        return BH1750_CMD_CONT_H_RES;
    case BH1750_MODE_CONT_HIGH2:
        return BH1750_CMD_CONT_H_RES2;
    case BH1750_MODE_CONT_LOW:
        return BH1750_CMD_CONT_L_RES;
    case BH1750_MODE_ONE_HIGH:
        return BH1750_CMD_ONE_H_RES;
    case BH1750_MODE_ONE_HIGH2:
        return BH1750_CMD_ONE_H_RES2;
    case BH1750_MODE_ONE_LOW:
        return BH1750_CMD_ONE_L_RES;
    default:
        return BH1750_CMD_CONT_H_RES;
    }
}

int bh1750_init(bh1750_handle_t *handle, iot_i2c_t *i2c, uint16_t address)
{
    uint8_t cmd;

    if (handle == NULL || i2c == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : BH1750_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->mode = BH1750_MODE_CONT_HIGH;
    handle->initialized = false;

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("bh1750", "probe failed at 0x%02X", handle->i2c.address);
        return (int)IOT_NOT_FOUND;
    }

    cmd = BH1750_CMD_POWER_ON;
    if (driver_i2c_write(&handle->i2c, &cmd, 1U) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }

    cmd = bh1750_mode_command(handle->mode);
    if (driver_i2c_write(&handle->i2c, &cmd, 1U) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    driver_delay_ms(BH1750_MEASURE_DELAY_MS);

    handle->initialized = true;
    LOG_I("bh1750", "init ok at 0x%02X", handle->i2c.address);
    return DRIVER_OK;
}

int bh1750_deinit(bh1750_handle_t *handle)
{
    uint8_t cmd = BH1750_CMD_POWER_DOWN;

    if (handle == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    if (handle->i2c.handle != NULL) {
        (void)driver_i2c_write(&handle->i2c, &cmd, 1U);
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int bh1750_set_mode(bh1750_handle_t *handle, bh1750_mode_t mode)
{
    uint8_t cmd;

    if (handle == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }

    cmd = bh1750_mode_command(mode);
    if (driver_i2c_write(&handle->i2c, &cmd, 1U) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    handle->mode = mode;
    driver_delay_ms(BH1750_MEASURE_DELAY_MS);
    return DRIVER_OK;
}

int bh1750_read_raw(bh1750_handle_t *handle, uint16_t *raw)
{
    uint8_t d[2];

    if (handle == NULL || raw == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }

    /* 单次模式需要重新触发一次转换 */
    if (handle->mode >= BH1750_MODE_ONE_HIGH) {
        uint8_t cmd = bh1750_mode_command(handle->mode);
        if (driver_i2c_write(&handle->i2c, &cmd, 1U) != DRIVER_OK) {
            return (int)IOT_IO_ERROR;
        }
        driver_delay_ms(BH1750_MEASURE_DELAY_MS);
    }

    if (driver_i2c_read(&handle->i2c, d, sizeof(d)) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    *raw = (uint16_t)(((uint16_t)d[0] << 8) | d[1]);
    return DRIVER_OK;
}

int bh1750_read(bh1750_handle_t *handle, float *lux)
{
    uint16_t raw = 0U;
    int ret;

    if (lux == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    ret = bh1750_read_raw(handle, &raw);
    if (ret != DRIVER_OK) {
        return ret;
    }
    *lux = (float)raw / BH1750_LUX_DIVISOR;
    return DRIVER_OK;
}
