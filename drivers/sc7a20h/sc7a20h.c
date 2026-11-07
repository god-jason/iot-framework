#include "sc7a20h.h"
#include "sc7a20h_register.h"
#include "log.h"

#define SC7A20H_TAG "sc7a20h"

static const uint8_t s_odr_code[SC7A20H_ODR_COUNT] = {
    SC7A20H_ODR_CODE_1HZ,
    SC7A20H_ODR_CODE_10HZ,
    SC7A20H_ODR_CODE_25HZ,
    SC7A20H_ODR_CODE_50HZ,
    SC7A20H_ODR_CODE_100HZ,
    SC7A20H_ODR_CODE_200HZ,
    SC7A20H_ODR_CODE_400HZ
};

static const uint8_t s_fs_code[SC7A20H_RANGE_COUNT] = {
    SC7A20H_FS_CODE_2G,
    SC7A20H_FS_CODE_4G,
    SC7A20H_FS_CODE_8G,
    SC7A20H_FS_CODE_16G
};

static const float s_g_per_lsb[SC7A20H_RANGE_COUNT] = {
    SC7A20H_SENS_G_PER_LSB_2G,
    SC7A20H_SENS_G_PER_LSB_4G,
    SC7A20H_SENS_G_PER_LSB_8G,
    SC7A20H_SENS_G_PER_LSB_16G
};

static int sc7a20h_write_reg(sc7a20h_handle_t *handle, uint8_t reg, uint8_t value)
{
    return driver_i2c_write_reg(&handle->i2c, reg, &value, 1U);
}

/* CTRL_REG1：输出率 + 普通模式 + 三轴使能 */
static uint8_t sc7a20h_ctrl1_value(sc7a20h_handle_t *handle)
{
    return (uint8_t)((uint8_t)(s_odr_code[handle->odr] << 4) |
                     SC7A20H_CTRL1_LPEN_NORMAL | SC7A20H_CTRL1_AXIS_ENABLE);
}

/* CTRL_REG4：BDU + 量程（bit3 保留，不写） */
static uint8_t sc7a20h_ctrl4_value(sc7a20h_handle_t *handle)
{
    return (uint8_t)(SC7A20H_CTRL4_BDU | s_fs_code[handle->range]);
}

static int sc7a20h_write_ctrl1(sc7a20h_handle_t *handle)
{
    if (sc7a20h_write_reg(handle, SC7A20H_REG_CTRL_REG1, sc7a20h_ctrl1_value(handle))
        != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    return DRIVER_OK;
}

static int sc7a20h_write_ctrl4(sc7a20h_handle_t *handle)
{
    if (sc7a20h_write_reg(handle, SC7A20H_REG_CTRL_REG4, sc7a20h_ctrl4_value(handle))
        != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    return DRIVER_OK;
}

int sc7a20h_init(sc7a20h_handle_t *handle, port_i2c_handle_t *i2c, uint16_t address)
{
    uint8_t id = 0U;

    if (handle == NULL || i2c == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : SC7A20H_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->range = SC7A20H_RANGE_2G;
    handle->odr = SC7A20H_ODR_100HZ;
    handle->g_per_lsb = s_g_per_lsb[SC7A20H_RANGE_2G];
    handle->initialized = false;

    if (driver_i2c_read_reg(&handle->i2c, SC7A20H_REG_WHO_AM_I, &id, 1U) != DRIVER_OK) {
        LOG_E(SC7A20H_TAG, "read who_am_i failed at 0x%02X", handle->i2c.address);
        return (int)PORT_IO_ERROR;
    }
    if (id != SC7A20H_WHO_AM_I_VALUE) {
        LOG_E(SC7A20H_TAG, "who_am_i mismatch: 0x%02X", id);
        return (int)PORT_NOT_FOUND;
    }

    if (sc7a20h_write_ctrl4(handle) != DRIVER_OK || sc7a20h_write_ctrl1(handle) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    driver_delay_ms(5U);

    handle->initialized = true;
    LOG_I(SC7A20H_TAG, "init ok at 0x%02X", handle->i2c.address);
    return DRIVER_OK;
}

int sc7a20h_deinit(sc7a20h_handle_t *handle)
{
    if (handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    if (handle->initialized && handle->i2c.handle != NULL) {
        (void)sc7a20h_write_reg(handle, SC7A20H_REG_CTRL_REG1, SC7A20H_CTRL1_LPEN_NORMAL);
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int sc7a20h_read_raw(sc7a20h_handle_t *handle, int16_t accel[3])
{
    uint8_t d[6];

    if (handle == NULL || accel == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    if (driver_i2c_read_reg(&handle->i2c, SC7A20H_REG_OUT_X_L, d, sizeof(d)) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }

    accel[0] = driver_le16(&d[0]);
    accel[1] = driver_le16(&d[2]);
    accel[2] = driver_le16(&d[4]);
    return DRIVER_OK;
}

int sc7a20h_read(sc7a20h_handle_t *handle, sc7a20h_accel_t *accel)
{
    int16_t raw[3];
    int ret;

    if (accel == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    ret = sc7a20h_read_raw(handle, raw);
    if (ret != DRIVER_OK) {
        return ret;
    }

    accel->x = (float)raw[0] * handle->g_per_lsb;
    accel->y = (float)raw[1] * handle->g_per_lsb;
    accel->z = (float)raw[2] * handle->g_per_lsb;
    return DRIVER_OK;
}

int sc7a20h_set_range(sc7a20h_handle_t *handle, sc7a20h_range_t range)
{
    sc7a20h_range_t old;

    if (handle == NULL || !handle->initialized ||
        (unsigned)range >= (unsigned)SC7A20H_RANGE_COUNT) {
        return (int)PORT_INVALID_PARAM;
    }

    old = handle->range;
    handle->range = range;
    if (sc7a20h_write_ctrl4(handle) != DRIVER_OK) {
        handle->range = old;
        return (int)PORT_IO_ERROR;
    }
    handle->g_per_lsb = s_g_per_lsb[range];
    return DRIVER_OK;
}

int sc7a20h_set_output_rate(sc7a20h_handle_t *handle, sc7a20h_odr_t odr)
{
    sc7a20h_odr_t old;

    if (handle == NULL || !handle->initialized || (unsigned)odr >= (unsigned)SC7A20H_ODR_COUNT) {
        return (int)PORT_INVALID_PARAM;
    }

    old = handle->odr;
    handle->odr = odr;
    if (sc7a20h_write_ctrl1(handle) != DRIVER_OK) {
        handle->odr = old;
        return (int)PORT_IO_ERROR;
    }
    return DRIVER_OK;
}
