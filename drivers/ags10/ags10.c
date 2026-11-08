#include "ags10.h"
#include "ags10_register.h"
#include "log.h"

/* CRC-8：poly 0x31 反射（0x8C），init 0xFF */
static uint8_t ags10_crc(const uint8_t *data, size_t length)
{
    uint8_t crc = AGS10_CRC_INIT;
    size_t i;
    uint8_t b;

    for (i = 0U; i < length; i++) {
        crc ^= data[i];
        for (b = 0U; b < 8U; b++) {
            if ((crc & 0x01U) != 0U) {
                crc = (uint8_t)((crc >> 1) ^ AGS10_CRC_POLY_REFLECTED);
            } else {
                crc = (uint8_t)(crc >> 1);
            }
        }
    }
    return crc;
}

static int ags10_read_reg(ags10_handle_t *handle, uint8_t reg,
                          uint8_t *data, size_t data_len)
{
    int ret;

    ret = driver_i2c_read_reg(&handle->i2c, reg, data, data_len + 1U);
    if (ret != DRIVER_OK) {
        return ret;
    }
    if (ags10_crc(data, data_len) != data[data_len]) {
        LOG_E("ags10", "crc error on reg 0x%02X", (unsigned)reg);
        return (int)HAL_CRC_ERROR;
    }
    return DRIVER_OK;
}

int ags10_get_version(ags10_handle_t *handle, uint16_t *version)
{
    uint8_t raw[3];
    int ret;

    if (handle == NULL || version == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    ret = ags10_read_reg(handle, AGS10_REG_VERSION, raw, 2U);
    if (ret != DRIVER_OK) {
        return ret;
    }
    *version = (uint16_t)(((uint16_t)raw[0] << 8) | raw[1]);
    return DRIVER_OK;
}

int ags10_init(ags10_handle_t *handle, hal_i2c_t *i2c, uint16_t address)
{
    uint16_t version = 0U;
    int ret;

    if (handle == NULL || i2c == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : AGS10_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->version = 0U;
    handle->tvoc_scale = 0.0f;
    handle->initialized = false;

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("ags10", "probe failed at 0x%02X", (unsigned)handle->i2c.address);
        return (int)HAL_NOT_FOUND;
    }

    /* ID 校验：版本号 + CRC */
    ret = ags10_get_version(handle, &version);
    if (ret != DRIVER_OK) {
        LOG_E("ags10", "version read failed: %d", ret);
        return (int)HAL_NOT_FOUND;
    }

    handle->version = version;
    handle->initialized = true;
    LOG_I("ags10", "init ok, version=0x%04X", (unsigned)version);
    return DRIVER_OK;
}

int ags10_deinit(ags10_handle_t *handle)
{
    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int ags10_set_tvoc_scale(ags10_handle_t *handle, float scale)
{
    if (handle == NULL || !handle->initialized || scale < 0.0f) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->tvoc_scale = scale;
    return DRIVER_OK;
}

int ags10_read(ags10_handle_t *handle, ags10_data_t *data)
{
    uint8_t raw[5];
    uint32_t value;
    int ret;

    if (handle == NULL || data == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }

    ret = ags10_read_reg(handle, AGS10_REG_VOC, raw, 4U);
    if (ret != DRIVER_OK) {
        return ret;
    }

    /* 4 字节大端电阻值 */
    value = ((uint32_t)raw[0] << 24) | ((uint32_t)raw[1] << 16) |
            ((uint32_t)raw[2] << 8) | (uint32_t)raw[3];

    data->raw = value;
    data->resistance_ohm = (float)value;
    data->tvoc_ppm_est = 0.0f;
    data->tvoc_valid = false;
    if (handle->tvoc_scale > 0.0f) {
        /* 线性近似，真实关系为幂函数，需按 datasheet 标定 */
        data->tvoc_ppm_est = handle->tvoc_scale * (float)value;
        data->tvoc_valid = true;
    }
    return DRIVER_OK;
}
