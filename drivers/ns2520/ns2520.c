#include "ns2520.h"
#include "ns2520_register.h"
#include "log.h"

#define NS2520_TAG "ns2520"

static const uint8_t s_odr_code[NS2520_ODR_COUNT] = {
    NS2520_ODR_CODE_1HZ,
    NS2520_ODR_CODE_10HZ,
    NS2520_ODR_CODE_25HZ,
    NS2520_ODR_CODE_50HZ,
    NS2520_ODR_CODE_100HZ,
    NS2520_ODR_CODE_200HZ,
    NS2520_ODR_CODE_400HZ
};

static const uint8_t s_fs_code[NS2520_RANGE_COUNT] = {
    NS2520_FS_CODE_2G,
    NS2520_FS_CODE_4G,
    NS2520_FS_CODE_8G,
    NS2520_FS_CODE_16G
};

static const float s_g_per_lsb[NS2520_RANGE_COUNT] = {
    NS2520_SENS_G_PER_LSB_2G,
    NS2520_SENS_G_PER_LSB_4G,
    NS2520_SENS_G_PER_LSB_8G,
    NS2520_SENS_G_PER_LSB_16G
};

static int ns2520_write_reg(ns2520_handle_t *handle, uint8_t reg, uint8_t value)
{
    return driver_i2c_write_reg(&handle->i2c, reg, &value, 1U);
}

static int ns2520_read_reg(ns2520_handle_t *handle, uint8_t reg, uint8_t *value)
{
    return driver_i2c_read_reg(&handle->i2c, reg, value, 1U);
}

/* CTRL_REG1：输出率 + 普通模式 + 三轴使能 */
static uint8_t ns2520_ctrl1_value(ns2520_handle_t *handle)
{
    return (uint8_t)((uint8_t)(s_odr_code[handle->odr] << 4) |
                     NS2520_CTRL1_LPEN_NORMAL | NS2520_CTRL1_AXIS_ENABLE);
}

/* CTRL_REG4：BDU + 量程（bit3 保留，不写） */
static uint8_t ns2520_ctrl4_value(ns2520_handle_t *handle)
{
    return (uint8_t)(NS2520_CTRL4_BDU | s_fs_code[handle->range]);
}

static int ns2520_write_ctrl1(ns2520_handle_t *handle)
{
    if (ns2520_write_reg(handle, NS2520_REG_CTRL_REG1, ns2520_ctrl1_value(handle)) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    return DRIVER_OK;
}

static int ns2520_write_ctrl4(ns2520_handle_t *handle)
{
    if (ns2520_write_reg(handle, NS2520_REG_CTRL_REG4, ns2520_ctrl4_value(handle)) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    return DRIVER_OK;
}

/* 写通路自检：回读 CTRL_REG1 / CTRL_REG4 并与写入值比较 */
static int ns2520_verify_config(ns2520_handle_t *handle)
{
    uint8_t value = 0U;

    if (ns2520_read_reg(handle, NS2520_REG_CTRL_REG1, &value) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    if (value != ns2520_ctrl1_value(handle)) {
        LOG_E(NS2520_TAG, "ctrl_reg1 verify failed: 0x%02X", value);
        return (int)HAL_NOT_FOUND;
    }

    if (ns2520_read_reg(handle, NS2520_REG_CTRL_REG4, &value) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    if (value != ns2520_ctrl4_value(handle)) {
        LOG_E(NS2520_TAG, "ctrl_reg4 verify failed: 0x%02X", value);
        return (int)HAL_NOT_FOUND;
    }
    return DRIVER_OK;
}

int ns2520_init(ns2520_handle_t *handle, hal_i2c_t *i2c, uint16_t address)
{
    if (handle == NULL || i2c == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : NS2520_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->range = NS2520_RANGE_2G;
    handle->odr = NS2520_ODR_100HZ;
    handle->g_per_lsb = s_g_per_lsb[NS2520_RANGE_2G];
    handle->initialized = false;

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E(NS2520_TAG, "probe failed at 0x%02X", handle->i2c.address);
        return (int)HAL_NOT_FOUND;
    }

    if (ns2520_write_ctrl4(handle) != DRIVER_OK || ns2520_write_ctrl1(handle) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    if (ns2520_verify_config(handle) != DRIVER_OK) {
        return (int)HAL_NOT_FOUND;
    }
    driver_delay_ms(5U);

    handle->initialized = true;
    LOG_I(NS2520_TAG, "init ok at 0x%02X", handle->i2c.address);
    return DRIVER_OK;
}

int ns2520_deinit(ns2520_handle_t *handle)
{
    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    if (handle->initialized && handle->i2c.handle != NULL) {
        (void)ns2520_write_reg(handle, NS2520_REG_CTRL_REG1, NS2520_CTRL1_LPEN_NORMAL);
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int ns2520_read_raw(ns2520_handle_t *handle, int16_t accel[3])
{
    uint8_t d[6];

    if (handle == NULL || accel == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    if (driver_i2c_read_reg(&handle->i2c, NS2520_REG_OUT_X_L, d, sizeof(d)) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }

    accel[0] = driver_le16(&d[0]);
    accel[1] = driver_le16(&d[2]);
    accel[2] = driver_le16(&d[4]);
    return DRIVER_OK;
}

int ns2520_read(ns2520_handle_t *handle, ns2520_accel_t *accel)
{
    int16_t raw[3];
    int ret;

    if (accel == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    ret = ns2520_read_raw(handle, raw);
    if (ret != DRIVER_OK) {
        return ret;
    }

    accel->x = (float)raw[0] * handle->g_per_lsb;
    accel->y = (float)raw[1] * handle->g_per_lsb;
    accel->z = (float)raw[2] * handle->g_per_lsb;
    return DRIVER_OK;
}

int ns2520_set_range(ns2520_handle_t *handle, ns2520_range_e range)
{
    ns2520_range_e old;

    if (handle == NULL || !handle->initialized ||
        (unsigned)range >= (unsigned)NS2520_RANGE_COUNT) {
        return (int)HAL_INVALID_PARAM;
    }

    old = handle->range;
    handle->range = range;
    if (ns2520_write_ctrl4(handle) != DRIVER_OK) {
        handle->range = old;
        return (int)HAL_IO_ERROR;
    }
    handle->g_per_lsb = s_g_per_lsb[range];
    return DRIVER_OK;
}

int ns2520_set_output_rate(ns2520_handle_t *handle, ns2520_odr_e odr)
{
    ns2520_odr_e old;

    if (handle == NULL || !handle->initialized || (unsigned)odr >= (unsigned)NS2520_ODR_COUNT) {
        return (int)HAL_INVALID_PARAM;
    }

    old = handle->odr;
    handle->odr = odr;
    if (ns2520_write_ctrl1(handle) != DRIVER_OK) {
        handle->odr = old;
        return (int)HAL_IO_ERROR;
    }
    return DRIVER_OK;
}
