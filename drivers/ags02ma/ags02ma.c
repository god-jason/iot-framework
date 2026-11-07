#include "ags02ma.h"
#include "ags02ma_register.h"
#include "log.h"

/* CRC-8：poly 0x31 反射（0x8C），init 0xFF */
static uint8_t ags02ma_crc(const uint8_t *data, size_t length)
{
    uint8_t crc = AGS02MA_CRC_INIT;
    size_t i;
    uint8_t b;

    for (i = 0U; i < length; i++) {
        crc ^= data[i];
        for (b = 0U; b < 8U; b++) {
            if ((crc & 0x01U) != 0U) {
                crc = (uint8_t)((crc >> 1) ^ AGS02MA_CRC_POLY_REFLECTED);
            } else {
                crc = (uint8_t)(crc >> 1);
            }
        }
    }
    return crc;
}

/* 写寄存器地址后读 data_len 字节数据 + 1 字节 CRC */
static int ags02ma_read_reg(ags02ma_handle_t *handle, uint8_t reg,
                            uint8_t *data, size_t data_len)
{
    int ret;

    ret = driver_i2c_read_reg(&handle->i2c, reg, data, data_len + 1U);
    if (ret != DRIVER_OK) {
        return ret;
    }
    if (ags02ma_crc(data, data_len) != data[data_len]) {
        LOG_E("ags02ma", "crc error on reg 0x%02X", (unsigned)reg);
        return (int)PORT_CRC_ERROR;
    }
    return DRIVER_OK;
}

int ags02ma_get_version(ags02ma_handle_t *handle, uint16_t *version)
{
    uint8_t raw[3];
    int ret;

    if (handle == NULL || version == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    ret = ags02ma_read_reg(handle, AGS02MA_REG_VERSION, raw, 2U);
    if (ret != DRIVER_OK) {
        return ret;
    }
    *version = (uint16_t)(((uint16_t)raw[0] << 8) | raw[1]);
    return DRIVER_OK;
}

int ags02ma_init(ags02ma_handle_t *handle, port_i2c_handle_t *i2c, uint16_t address)
{
    uint16_t version = 0U;
    int ret;

    if (handle == NULL || i2c == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : AGS02MA_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->version = 0U;
    handle->eco2_scale = 0.0f;
    handle->initialized = false;

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("ags02ma", "probe failed at 0x%02X", (unsigned)handle->i2c.address);
        return (int)PORT_NOT_FOUND;
    }

    /* ID 校验：读版本号（CRC 必须通过） */
    ret = ags02ma_get_version(handle, &version);
    if (ret != DRIVER_OK) {
        LOG_E("ags02ma", "version read failed: %d", ret);
        return (int)PORT_NOT_FOUND;
    }

    handle->version = version;
    handle->initialized = true;
    LOG_I("ags02ma", "init ok, version=0x%04X", (unsigned)version);
    return DRIVER_OK;
}

int ags02ma_deinit(ags02ma_handle_t *handle)
{
    if (handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int ags02ma_set_eco2_scale(ags02ma_handle_t *handle, float scale)
{
    if (handle == NULL || !handle->initialized || scale < 0.0f) {
        return (int)PORT_INVALID_PARAM;
    }
    handle->eco2_scale = scale;
    return DRIVER_OK;
}

int ags02ma_read(ags02ma_handle_t *handle, ags02ma_data_t *data)
{
    uint8_t raw[5];
    uint32_t tvoc;
    int ret;

    if (handle == NULL || data == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }

    ret = ags02ma_read_reg(handle, AGS02MA_REG_TVOC, raw, 4U);
    if (ret != DRIVER_OK) {
        return ret;
    }

    /* TVOC 为 4 字节大端，单位 ppb */
    tvoc = ((uint32_t)raw[0] << 24) | ((uint32_t)raw[1] << 16) |
           ((uint32_t)raw[2] << 8) | (uint32_t)raw[3];
    if (tvoc > AGS02MA_TVOC_MAX_PPB) {
        LOG_W("ags02ma", "tvoc %luppb out of range", (unsigned long)tvoc);
        return (int)PORT_INVALID_PARAM;
    }

    data->tvoc_ppb = tvoc;
    data->eco2_ppm_est = 0.0f;
    data->eco2_valid = false;
    if (handle->eco2_scale > 0.0f) {
        /* 线性近似，Aosong 的 eCO2 换算规则需按 datasheet 标定 */
        data->eco2_ppm_est = handle->eco2_scale * (float)tvoc;
        data->eco2_valid = true;
    }
    return DRIVER_OK;
}
