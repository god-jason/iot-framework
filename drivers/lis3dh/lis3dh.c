#include "lis3dh.h"
#include "lis3dh_register.h"
#include "log.h"

#define LIS3DH_TAG "lis3dh"

static const uint8_t s_odr_code[LIS3DH_ODR_COUNT] = {
    LIS3DH_ODR_CODE_1HZ,
    LIS3DH_ODR_CODE_10HZ,
    LIS3DH_ODR_CODE_25HZ,
    LIS3DH_ODR_CODE_50HZ,
    LIS3DH_ODR_CODE_100HZ,
    LIS3DH_ODR_CODE_200HZ,
    LIS3DH_ODR_CODE_400HZ
};

static const uint8_t s_fs_code[LIS3DH_RANGE_COUNT] = {
    LIS3DH_FS_CODE_2G,
    LIS3DH_FS_CODE_4G,
    LIS3DH_FS_CODE_8G,
    LIS3DH_FS_CODE_16G
};

static const float s_g_per_lsb[LIS3DH_RANGE_COUNT] = {
    LIS3DH_SENS_G_PER_LSB_2G,
    LIS3DH_SENS_G_PER_LSB_4G,
    LIS3DH_SENS_G_PER_LSB_8G,
    LIS3DH_SENS_G_PER_LSB_16G
};

static int lis3dh_write_reg(lis3dh_handle_t *handle, uint8_t reg, uint8_t value)
{
    return driver_i2c_write_reg(&handle->i2c, reg, &value, 1U);
}

/* CTRL_REG1：输出率 + 普通模式 + 三轴使能 */
static uint8_t lis3dh_ctrl1_value(lis3dh_handle_t *handle)
{
    return (uint8_t)((uint8_t)(s_odr_code[handle->odr] << 4) |
                     LIS3DH_CTRL1_LPEN_NORMAL | LIS3DH_CTRL1_AXIS_ENABLE);
}

/* CTRL_REG4：BDU + 高分辨率（12 位）+ 量程 */
static uint8_t lis3dh_ctrl4_value(lis3dh_handle_t *handle)
{
    return (uint8_t)(LIS3DH_CTRL4_BDU | LIS3DH_CTRL4_HR | s_fs_code[handle->range]);
}

static int lis3dh_write_ctrl1(lis3dh_handle_t *handle)
{
    if (lis3dh_write_reg(handle, LIS3DH_REG_CTRL_REG1, lis3dh_ctrl1_value(handle)) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    return DRIVER_OK;
}

static int lis3dh_write_ctrl4(lis3dh_handle_t *handle)
{
    if (lis3dh_write_reg(handle, LIS3DH_REG_CTRL_REG4, lis3dh_ctrl4_value(handle)) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    return DRIVER_OK;
}

int lis3dh_init(lis3dh_handle_t *handle, iot_i2c_t *i2c, uint16_t address)
{
    uint8_t id = 0U;

    if (handle == NULL || i2c == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : LIS3DH_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->range = LIS3DH_RANGE_2G;
    handle->odr = LIS3DH_ODR_100HZ;
    handle->g_per_lsb = s_g_per_lsb[LIS3DH_RANGE_2G];
    handle->initialized = false;

    if (driver_i2c_read_reg(&handle->i2c, LIS3DH_REG_WHO_AM_I, &id, 1U) != DRIVER_OK) {
        LOG_E(LIS3DH_TAG, "read who_am_i failed at 0x%02X", handle->i2c.address);
        return (int)IOT_IO_ERROR;
    }
    if (id != LIS3DH_WHO_AM_I_VALUE) {
        LOG_E(LIS3DH_TAG, "who_am_i mismatch: 0x%02X", id);
        return (int)IOT_NOT_FOUND;
    }

    if (lis3dh_write_ctrl4(handle) != DRIVER_OK || lis3dh_write_ctrl1(handle) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    driver_delay_ms(5U);

    handle->initialized = true;
    LOG_I(LIS3DH_TAG, "init ok at 0x%02X", handle->i2c.address);
    return DRIVER_OK;
}

int lis3dh_deinit(lis3dh_handle_t *handle)
{
    if (handle == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    if (handle->initialized && handle->i2c.handle != NULL) {
        (void)lis3dh_write_reg(handle, LIS3DH_REG_CTRL_REG1, LIS3DH_CTRL1_LPEN_NORMAL);
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int lis3dh_read_raw(lis3dh_handle_t *handle, int16_t accel[3])
{
    uint8_t d[6];

    if (handle == NULL || accel == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    if (driver_i2c_read_reg(&handle->i2c, LIS3DH_REG_OUT_X_L, d, sizeof(d)) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }

    accel[0] = driver_le16(&d[0]);
    accel[1] = driver_le16(&d[2]);
    accel[2] = driver_le16(&d[4]);
    return DRIVER_OK;
}

int lis3dh_read(lis3dh_handle_t *handle, lis3dh_accel_t *accel)
{
    int16_t raw[3];
    int ret;

    if (accel == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    ret = lis3dh_read_raw(handle, raw);
    if (ret != DRIVER_OK) {
        return ret;
    }

    accel->x = (float)raw[0] * handle->g_per_lsb;
    accel->y = (float)raw[1] * handle->g_per_lsb;
    accel->z = (float)raw[2] * handle->g_per_lsb;
    return DRIVER_OK;
}

int lis3dh_set_range(lis3dh_handle_t *handle, lis3dh_range_t range)
{
    lis3dh_range_t old;

    if (handle == NULL || !handle->initialized || (unsigned)range >= (unsigned)LIS3DH_RANGE_COUNT) {
        return (int)IOT_INVALID_PARAM;
    }

    old = handle->range;
    handle->range = range;
    if (lis3dh_write_ctrl4(handle) != DRIVER_OK) {
        handle->range = old;
        return (int)IOT_IO_ERROR;
    }
    handle->g_per_lsb = s_g_per_lsb[range];
    return DRIVER_OK;
}

int lis3dh_set_output_rate(lis3dh_handle_t *handle, lis3dh_odr_t odr)
{
    lis3dh_odr_t old;

    if (handle == NULL || !handle->initialized || (unsigned)odr >= (unsigned)LIS3DH_ODR_COUNT) {
        return (int)IOT_INVALID_PARAM;
    }

    old = handle->odr;
    handle->odr = odr;
    if (lis3dh_write_ctrl1(handle) != DRIVER_OK) {
        handle->odr = old;
        return (int)IOT_IO_ERROR;
    }
    return DRIVER_OK;
}
