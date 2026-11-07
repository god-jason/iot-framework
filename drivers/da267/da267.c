#include "da267.h"
#include "da267_register.h"
#include "log.h"

#define DA267_TAG "da267"

static const uint8_t s_odr_code[DA267_ODR_COUNT] = {
    DA267_ODR_CODE_1HZ,
    DA267_ODR_CODE_10HZ,
    DA267_ODR_CODE_25HZ,
    DA267_ODR_CODE_50HZ,
    DA267_ODR_CODE_100HZ,
    DA267_ODR_CODE_200HZ,
    DA267_ODR_CODE_400HZ
};

static const uint8_t s_fs_code[DA267_RANGE_COUNT] = {
    DA267_FS_CODE_2G,
    DA267_FS_CODE_4G,
    DA267_FS_CODE_8G,
    DA267_FS_CODE_16G
};

static const float s_g_per_lsb[DA267_RANGE_COUNT] = {
    DA267_SENS_G_PER_LSB_2G,
    DA267_SENS_G_PER_LSB_4G,
    DA267_SENS_G_PER_LSB_8G,
    DA267_SENS_G_PER_LSB_16G
};

static int da267_write_reg(da267_handle_t *handle, uint8_t reg, uint8_t value)
{
    return driver_i2c_write_reg(&handle->i2c, reg, &value, 1U);
}

static int da267_read_reg(da267_handle_t *handle, uint8_t reg, uint8_t *value)
{
    return driver_i2c_read_reg(&handle->i2c, reg, value, 1U);
}

/* CTRL_REG1：输出率 + 普通模式 + 三轴使能 */
static uint8_t da267_ctrl1_value(da267_handle_t *handle)
{
    return (uint8_t)((uint8_t)(s_odr_code[handle->odr] << 4) |
                     DA267_CTRL1_LPEN_NORMAL | DA267_CTRL1_AXIS_ENABLE);
}

/* CTRL_REG4：BDU + 量程（默认 12 位高分辨率，bit3 保留不写） */
static uint8_t da267_ctrl4_value(da267_handle_t *handle)
{
    return (uint8_t)(DA267_CTRL4_BDU | s_fs_code[handle->range]);
}

static int da267_write_ctrl1(da267_handle_t *handle)
{
    if (da267_write_reg(handle, DA267_REG_CTRL_REG1, da267_ctrl1_value(handle)) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    return DRIVER_OK;
}

static int da267_write_ctrl4(da267_handle_t *handle)
{
    if (da267_write_reg(handle, DA267_REG_CTRL_REG4, da267_ctrl4_value(handle)) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    return DRIVER_OK;
}

/* 写通路自检：回读 CTRL_REG1 / CTRL_REG4 并与写入值比较 */
static int da267_verify_config(da267_handle_t *handle)
{
    uint8_t value = 0U;

    if (da267_read_reg(handle, DA267_REG_CTRL_REG1, &value) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    if (value != da267_ctrl1_value(handle)) {
        LOG_E(DA267_TAG, "ctrl_reg1 verify failed: 0x%02X", value);
        return (int)IOT_NOT_FOUND;
    }

    if (da267_read_reg(handle, DA267_REG_CTRL_REG4, &value) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    if (value != da267_ctrl4_value(handle)) {
        LOG_E(DA267_TAG, "ctrl_reg4 verify failed: 0x%02X", value);
        return (int)IOT_NOT_FOUND;
    }
    return DRIVER_OK;
}

int da267_init(da267_handle_t *handle, iot_i2c_t *i2c, uint16_t address)
{
    if (handle == NULL || i2c == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : DA267_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->range = DA267_RANGE_2G;
    handle->odr = DA267_ODR_100HZ;
    handle->g_per_lsb = s_g_per_lsb[DA267_RANGE_2G];
    handle->initialized = false;

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E(DA267_TAG, "probe failed at 0x%02X", handle->i2c.address);
        return (int)IOT_NOT_FOUND;
    }

    if (da267_write_ctrl4(handle) != DRIVER_OK || da267_write_ctrl1(handle) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    if (da267_verify_config(handle) != DRIVER_OK) {
        return (int)IOT_NOT_FOUND;
    }
    driver_delay_ms(5U);

    handle->initialized = true;
    LOG_I(DA267_TAG, "init ok at 0x%02X", handle->i2c.address);
    return DRIVER_OK;
}

int da267_deinit(da267_handle_t *handle)
{
    if (handle == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    if (handle->initialized && handle->i2c.handle != NULL) {
        /* 关闭三轴，器件进入低功耗 */
        (void)da267_write_reg(handle, DA267_REG_CTRL_REG1, DA267_CTRL1_LPEN_NORMAL);
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int da267_read_raw(da267_handle_t *handle, int16_t accel[3])
{
    uint8_t d[6];

    if (handle == NULL || accel == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    if (driver_i2c_read_reg(&handle->i2c, DA267_REG_OUT_X_L, d, sizeof(d)) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }

    accel[0] = driver_le16(&d[0]);
    accel[1] = driver_le16(&d[2]);
    accel[2] = driver_le16(&d[4]);
    return DRIVER_OK;
}

int da267_read(da267_handle_t *handle, da267_accel_t *accel)
{
    int16_t raw[3];
    int ret;

    if (accel == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    ret = da267_read_raw(handle, raw);
    if (ret != DRIVER_OK) {
        return ret;
    }

    accel->x = (float)raw[0] * handle->g_per_lsb;
    accel->y = (float)raw[1] * handle->g_per_lsb;
    accel->z = (float)raw[2] * handle->g_per_lsb;
    return DRIVER_OK;
}

int da267_set_range(da267_handle_t *handle, da267_range_t range)
{
    da267_range_t old;

    if (handle == NULL || !handle->initialized || (unsigned)range >= (unsigned)DA267_RANGE_COUNT) {
        return (int)IOT_INVALID_PARAM;
    }

    old = handle->range;
    handle->range = range;
    if (da267_write_ctrl4(handle) != DRIVER_OK) {
        handle->range = old;
        return (int)IOT_IO_ERROR;
    }
    handle->g_per_lsb = s_g_per_lsb[range];
    return DRIVER_OK;
}

int da267_set_output_rate(da267_handle_t *handle, da267_odr_t odr)
{
    da267_odr_t old;

    if (handle == NULL || !handle->initialized || (unsigned)odr >= (unsigned)DA267_ODR_COUNT) {
        return (int)IOT_INVALID_PARAM;
    }

    old = handle->odr;
    handle->odr = odr;
    if (da267_write_ctrl1(handle) != DRIVER_OK) {
        handle->odr = old;
        return (int)IOT_IO_ERROR;
    }
    return DRIVER_OK;
}
