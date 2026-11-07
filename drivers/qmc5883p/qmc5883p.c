#include "qmc5883p.h"
#include "qmc5883p_register.h"
#include "log.h"
#include <math.h>

/* 灵敏度表：索引与 qmc5883p_range_t（即 CTRL2.RNG 编码）一致 */
static const float s_lsb_per_gauss[QMC5883P_RANGE_COUNT] = {
    QMC5883P_LSB_PER_GAUSS_2G,
    QMC5883P_LSB_PER_GAUSS_8G,
    QMC5883P_LSB_PER_GAUSS_12G,
    QMC5883P_LSB_PER_GAUSS_30G
};

static int qmc5883p_write_reg(qmc5883p_handle_t *handle, uint8_t reg, uint8_t value)
{
    return (int)driver_i2c_write_reg(&handle->i2c, reg, &value, 1U);
}

static int qmc5883p_read_regs(qmc5883p_handle_t *handle, uint8_t reg, uint8_t *buf, size_t len)
{
    return (int)driver_i2c_read_reg(&handle->i2c, reg, buf, len);
}

static void qmc5883p_update_sensitivity(qmc5883p_handle_t *handle)
{
    handle->ut_per_lsb = QMC5883P_UT_PER_GAUSS / s_lsb_per_gauss[handle->range];
}

/* 写寄存器并回读指定位域：确认写通路与配置生效 */
static int qmc5883p_write_verified(qmc5883p_handle_t *handle, uint8_t reg,
                                   uint8_t value, uint8_t mask)
{
    uint8_t check = 0U;
    int ret;

    ret = qmc5883p_write_reg(handle, reg, value);
    if (ret != DRIVER_OK) {
        return ret;
    }
    ret = qmc5883p_read_regs(handle, reg, &check, 1U);
    if (ret != DRIVER_OK) {
        return ret;
    }
    if ((check & mask) != (value & mask)) {
        LOG_E("qmc5883p", "reg 0x%02X readback 0x%02X != 0x%02X",
              (unsigned)reg, (unsigned)check, (unsigned)value);
        return (int)IOT_HW_ERROR;
    }
    return DRIVER_OK;
}

int qmc5883p_soft_reset(qmc5883p_handle_t *handle)
{
    uint8_t suspend;
    int ret;

    if (handle == NULL || handle->i2c.handle == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    /* 数据手册未给出可靠的软复位位：先切到 suspend 让状态机复位，
     * 再按"量程 -> 模式"的顺序重新下发配置（顺序不能倒，否则模式会被重置）。 */
    suspend = (uint8_t)(handle->ctrl1 & (uint8_t)~QMC5883P_CTRL1_MODE_MASK);
    ret = qmc5883p_write_reg(handle, QMC5883P_REG_CTRL1, suspend);
    driver_delay_ms(QMC5883P_RESET_DELAY_MS);
    if (ret != DRIVER_OK) {
        return ret;
    }

    ret = qmc5883p_write_verified(handle, QMC5883P_REG_CTRL2, handle->ctrl2,
                                  QMC5883P_CTRL2_RNG_MASK);
    if (ret != DRIVER_OK) {
        return ret;
    }
    return qmc5883p_write_verified(handle, QMC5883P_REG_CTRL1, handle->ctrl1,
                                   (uint8_t)(QMC5883P_CTRL1_MODE_MASK |
                                             QMC5883P_CTRL1_ODR_MASK |
                                             QMC5883P_CTRL1_OSR_MASK));
}

int qmc5883p_set_range(qmc5883p_handle_t *handle, qmc5883p_range_t range)
{
    uint8_t value;
    int ret;

    if (handle == NULL || !handle->initialized ||
        (unsigned)range >= (unsigned)QMC5883P_RANGE_COUNT) {
        return (int)IOT_INVALID_PARAM;
    }

    value = (uint8_t)((handle->ctrl2 & (uint8_t)~QMC5883P_CTRL2_RNG_MASK) |
                      (uint8_t)((unsigned)range << QMC5883P_CTRL2_RNG_SHIFT));
    if (value != handle->ctrl2) {
        ret = qmc5883p_write_verified(handle, QMC5883P_REG_CTRL2, value,
                                      QMC5883P_CTRL2_RNG_MASK);
        if (ret != DRIVER_OK) {
            return ret;
        }
        handle->ctrl2 = value;
    }

    handle->range = range;
    qmc5883p_update_sensitivity(handle);
    return DRIVER_OK;
}

int qmc5883p_get_status(qmc5883p_handle_t *handle, uint8_t *status)
{
    if (handle == NULL || status == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    return qmc5883p_read_regs(handle, QMC5883P_REG_STATUS, status, 1U);
}

int qmc5883p_init(qmc5883p_handle_t *handle, iot_i2c_t *i2c, uint16_t address)
{
    uint8_t id = 0U;
    int ret;

    if (handle == NULL || i2c == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : QMC5883P_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->range = QMC5883P_RANGE_8G;
    handle->ctrl1 = QMC5883P_CTRL1_DEFAULT;
    handle->ctrl2 = QMC5883P_CTRL2_DEFAULT;
    handle->initialized = false;
    qmc5883p_update_sensitivity(handle);

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("qmc5883p", "probe failed at 0x%02X", (unsigned)handle->i2c.address);
        return (int)IOT_NOT_FOUND;
    }

    /* 0x00 为 Chip ID，QMC5883P 固定 0x80；不符说明不是该器件 */
    ret = qmc5883p_read_regs(handle, QMC5883P_REG_CHIP_ID, &id, 1U);
    if (ret != DRIVER_OK) {
        return ret;
    }
    if (id != QMC5883P_CHIP_ID_VALUE) {
        LOG_E("qmc5883p", "chip id 0x%02X != 0x80", (unsigned)id);
        return (int)IOT_NOT_FOUND;
    }

    ret = qmc5883p_soft_reset(handle);
    if (ret != DRIVER_OK) {
        LOG_E("qmc5883p", "config failed at 0x%02X", (unsigned)handle->i2c.address);
        return ret;
    }

    driver_delay_ms(QMC5883P_START_DELAY_MS);
    handle->initialized = true;
    LOG_I("qmc5883p", "init ok at 0x%02X, range=%d", (unsigned)handle->i2c.address,
          (int)handle->range);
    return DRIVER_OK;
}

int qmc5883p_deinit(qmc5883p_handle_t *handle)
{
    if (handle == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    if (handle->initialized && handle->i2c.handle != NULL) {
        uint8_t suspend = (uint8_t)(handle->ctrl1 & (uint8_t)~QMC5883P_CTRL1_MODE_MASK);
        (void)qmc5883p_write_reg(handle, QMC5883P_REG_CTRL1, suspend);
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int qmc5883p_read_raw(qmc5883p_handle_t *handle, int16_t xyz[3])
{
    uint8_t data[QMC5883P_DATA_LEN];
    uint8_t status = 0U;
    uint32_t start;
    int ret;

    if (handle == NULL || xyz == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }

    start = driver_tick_ms();
    for (;;) {
        ret = qmc5883p_read_regs(handle, QMC5883P_REG_STATUS, &status, 1U);
        if (ret != DRIVER_OK) {
            return ret;
        }
        if ((status & QMC5883P_STATUS_DRDY) != 0U) {
            break;
        }
        if ((uint32_t)(driver_tick_ms() - start) > QMC5883P_DATA_TIMEOUT_MS) {
            return (int)IOT_TIMEOUT;
        }
        driver_delay_ms(1U);
    }

    ret = qmc5883p_read_regs(handle, QMC5883P_REG_DATA_X_LSB, data, sizeof(data));
    if (ret != DRIVER_OK) {
        return ret;
    }

    xyz[0] = driver_le16(&data[0]);
    xyz[1] = driver_le16(&data[2]);
    xyz[2] = driver_le16(&data[4]);
    return DRIVER_OK;
}

int qmc5883p_read(qmc5883p_handle_t *handle, qmc5883p_data_t *data)
{
    int16_t xyz[3];
    uint8_t status = 0U;
    int ret;

    if (handle == NULL || data == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }

    ret = qmc5883p_read_raw(handle, xyz);
    if (ret != DRIVER_OK) {
        return ret;
    }

    data->mag_ut.x = (float)xyz[0] * handle->ut_per_lsb;
    data->mag_ut.y = (float)xyz[1] * handle->ut_per_lsb;
    data->mag_ut.z = (float)xyz[2] * handle->ut_per_lsb;

    data->heading_deg = atan2f(data->mag_ut.y, data->mag_ut.x) * QMC5883P_RAD_TO_DEG;
    if (data->heading_deg < 0.0f) {
        data->heading_deg += 360.0f;
    }

    if (qmc5883p_read_regs(handle, QMC5883P_REG_STATUS, &status, 1U) == DRIVER_OK) {
        data->data_ready = ((status & QMC5883P_STATUS_DRDY) != 0U);
        data->overflow = ((status & QMC5883P_STATUS_OVL) != 0U);
    } else {
        data->data_ready = true;
        data->overflow = false;
    }
    return DRIVER_OK;
}
