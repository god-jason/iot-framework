#include "lis2dh12.h"
#include "lis2dh12_register.h"
#include "log.h"

#define LIS2DH12_TAG "lis2dh12"

static const uint8_t s_odr_code[LIS2DH12_ODR_COUNT] = {
    LIS2DH12_ODR_CODE_1HZ,
    LIS2DH12_ODR_CODE_10HZ,
    LIS2DH12_ODR_CODE_25HZ,
    LIS2DH12_ODR_CODE_50HZ,
    LIS2DH12_ODR_CODE_100HZ,
    LIS2DH12_ODR_CODE_200HZ,
    LIS2DH12_ODR_CODE_400HZ
};

static const uint8_t s_fs_code[LIS2DH12_RANGE_COUNT] = {
    LIS2DH12_FS_CODE_2G,
    LIS2DH12_FS_CODE_4G,
    LIS2DH12_FS_CODE_8G,
    LIS2DH12_FS_CODE_16G
};

static const float s_g_per_lsb[LIS2DH12_RANGE_COUNT] = {
    LIS2DH12_SENS_G_PER_LSB_2G,
    LIS2DH12_SENS_G_PER_LSB_4G,
    LIS2DH12_SENS_G_PER_LSB_8G,
    LIS2DH12_SENS_G_PER_LSB_16G
};

static int lis2dh12_write_reg(lis2dh12_handle_t *handle, uint8_t reg, uint8_t value)
{
    return driver_i2c_write_reg(&handle->i2c, reg, &value, 1U);
}

/* CTRL_REG1：输出率 + 高分辨率 + 三轴使能 */
static uint8_t lis2dh12_ctrl1_value(lis2dh12_handle_t *handle)
{
    return (uint8_t)((uint8_t)(s_odr_code[handle->odr] << 4) |
                     LIS2DH12_CTRL1_LPEN_HR | LIS2DH12_CTRL1_AXIS_ENABLE);
}

/* CTRL_REG4：BDU + 量程（bit3 在 LIS2DH12 中保留，不写） */
static uint8_t lis2dh12_ctrl4_value(lis2dh12_handle_t *handle)
{
    return (uint8_t)(LIS2DH12_CTRL4_BDU | s_fs_code[handle->range]);
}

static int lis2dh12_write_ctrl1(lis2dh12_handle_t *handle)
{
    if (lis2dh12_write_reg(handle, LIS2DH12_REG_CTRL_REG1, lis2dh12_ctrl1_value(handle))
        != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    return DRIVER_OK;
}

static int lis2dh12_write_ctrl4(lis2dh12_handle_t *handle)
{
    if (lis2dh12_write_reg(handle, LIS2DH12_REG_CTRL_REG4, lis2dh12_ctrl4_value(handle))
        != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    return DRIVER_OK;
}

int lis2dh12_init(lis2dh12_handle_t *handle, hal_i2c_t *i2c, uint16_t address)
{
    uint8_t id = 0U;

    if (handle == NULL || i2c == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : LIS2DH12_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->range = LIS2DH12_RANGE_2G;
    handle->odr = LIS2DH12_ODR_100HZ;
    handle->g_per_lsb = s_g_per_lsb[LIS2DH12_RANGE_2G];
    handle->initialized = false;

    if (driver_i2c_read_reg(&handle->i2c, LIS2DH12_REG_WHO_AM_I, &id, 1U) != DRIVER_OK) {
        LOG_E(LIS2DH12_TAG, "read who_am_i failed at 0x%02X", handle->i2c.address);
        return (int)HAL_IO_ERROR;
    }
    if (id != LIS2DH12_WHO_AM_I_VALUE) {
        LOG_E(LIS2DH12_TAG, "who_am_i mismatch: 0x%02X", id);
        return (int)HAL_NOT_FOUND;
    }

    if (lis2dh12_write_ctrl4(handle) != DRIVER_OK || lis2dh12_write_ctrl1(handle) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    driver_delay_ms(5U);

    handle->initialized = true;
    LOG_I(LIS2DH12_TAG, "init ok at 0x%02X", handle->i2c.address);
    return DRIVER_OK;
}

int lis2dh12_deinit(lis2dh12_handle_t *handle)
{
    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    if (handle->initialized && handle->i2c.handle != NULL) {
        (void)lis2dh12_write_reg(handle, LIS2DH12_REG_CTRL_REG1, LIS2DH12_CTRL1_LPEN_HR);
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int lis2dh12_read_raw(lis2dh12_handle_t *handle, int16_t accel[3])
{
    uint8_t d[6];

    if (handle == NULL || accel == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    if (driver_i2c_read_reg(&handle->i2c, LIS2DH12_REG_OUT_X_L, d, sizeof(d)) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }

    accel[0] = driver_le16(&d[0]);
    accel[1] = driver_le16(&d[2]);
    accel[2] = driver_le16(&d[4]);
    return DRIVER_OK;
}

int lis2dh12_read(lis2dh12_handle_t *handle, lis2dh12_accel_t *accel)
{
    int16_t raw[3];
    int ret;

    if (accel == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    ret = lis2dh12_read_raw(handle, raw);
    if (ret != DRIVER_OK) {
        return ret;
    }

    accel->x = (float)raw[0] * handle->g_per_lsb;
    accel->y = (float)raw[1] * handle->g_per_lsb;
    accel->z = (float)raw[2] * handle->g_per_lsb;
    return DRIVER_OK;
}

int lis2dh12_set_range(lis2dh12_handle_t *handle, lis2dh12_range_e range)
{
    lis2dh12_range_e old;

    if (handle == NULL || !handle->initialized ||
        (unsigned)range >= (unsigned)LIS2DH12_RANGE_COUNT) {
        return (int)HAL_INVALID_PARAM;
    }

    old = handle->range;
    handle->range = range;
    if (lis2dh12_write_ctrl4(handle) != DRIVER_OK) {
        handle->range = old;
        return (int)HAL_IO_ERROR;
    }
    handle->g_per_lsb = s_g_per_lsb[range];
    return DRIVER_OK;
}

int lis2dh12_set_output_rate(lis2dh12_handle_t *handle, lis2dh12_odr_e odr)
{
    lis2dh12_odr_e old;

    if (handle == NULL || !handle->initialized || (unsigned)odr >= (unsigned)LIS2DH12_ODR_COUNT) {
        return (int)HAL_INVALID_PARAM;
    }

    old = handle->odr;
    handle->odr = odr;
    if (lis2dh12_write_ctrl1(handle) != DRIVER_OK) {
        handle->odr = old;
        return (int)HAL_IO_ERROR;
    }
    return DRIVER_OK;
}
