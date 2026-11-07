#include "qmc5883l.h"
#include "qmc5883l_register.h"
#include "log.h"
#include <math.h>

/* 灵敏度表：索引与 qmc5883l_range_t（即 CTRL1.RNG 编码）一致 */
static const float s_lsb_per_gauss[QMC5883L_RANGE_COUNT] = {
    QMC5883L_LSB_PER_GAUSS_2G,
    QMC5883L_LSB_PER_GAUSS_8G
};

static int qmc5883l_write_reg(qmc5883l_handle_t *handle, uint8_t reg, uint8_t value)
{
    return (int)driver_i2c_write_reg(&handle->i2c, reg, &value, 1U);
}

static int qmc5883l_read_regs(qmc5883l_handle_t *handle, uint8_t reg, uint8_t *buf, size_t len)
{
    return (int)driver_i2c_read_reg(&handle->i2c, reg, buf, len);
}

static void qmc5883l_update_sensitivity(qmc5883l_handle_t *handle)
{
    handle->ut_per_lsb = QMC5883L_UT_PER_GAUSS / s_lsb_per_gauss[handle->range];
}

/* 写 CTRL1 并回读：QMC5883L 没有可靠的 WHO_AM_I，用回读证明写通路可用 */
static int qmc5883l_write_ctrl1(qmc5883l_handle_t *handle, uint8_t value)
{
    uint8_t check = 0U;
    int ret;

    ret = qmc5883l_write_reg(handle, QMC5883L_REG_CTRL1, value);
    if (ret != DRIVER_OK) {
        return ret;
    }
    ret = qmc5883l_read_regs(handle, QMC5883L_REG_CTRL1, &check, 1U);
    if (ret != DRIVER_OK) {
        return ret;
    }
    if (check != value) {
        LOG_E("qmc5883l", "ctrl1 readback 0x%02X != 0x%02X",
              (unsigned)check, (unsigned)value);
        return (int)IOT_HW_ERROR;
    }
    return DRIVER_OK;
}

int qmc5883l_soft_reset(qmc5883l_handle_t *handle)
{
    int ret;

    if (handle == NULL || handle->i2c.handle == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    /* CTRL2.SOFT_RST 复位全部寄存器，复位后必须重新下发配置 */
    ret = qmc5883l_write_reg(handle, QMC5883L_REG_CTRL2, QMC5883L_CTRL2_SOFT_RST);
    driver_delay_ms(QMC5883L_RESET_DELAY_MS);
    if (ret != DRIVER_OK) {
        return ret;
    }

    ret = qmc5883l_write_reg(handle, QMC5883L_REG_SET_RESET, QMC5883L_SET_RESET_PERIOD);
    if (ret != DRIVER_OK) {
        return ret;
    }
    return qmc5883l_write_ctrl1(handle, handle->ctrl1);
}

int qmc5883l_set_range(qmc5883l_handle_t *handle, qmc5883l_range_t range)
{
    uint8_t old_ctrl1;
    uint8_t value;
    int ret;

    if (handle == NULL || !handle->initialized ||
        (unsigned)range >= (unsigned)QMC5883L_RANGE_COUNT) {
        return (int)IOT_INVALID_PARAM;
    }

    old_ctrl1 = handle->ctrl1;
    value = (uint8_t)((old_ctrl1 & (uint8_t)~QMC5883L_CTRL1_RNG_MASK) |
                      (uint8_t)((unsigned)range << QMC5883L_CTRL1_RNG_SHIFT));
    if (value == old_ctrl1) {
        handle->range = range;
        qmc5883l_update_sensitivity(handle);
        return DRIVER_OK;
    }

    ret = qmc5883l_write_ctrl1(handle, value);
    if (ret != DRIVER_OK) {
        return ret;
    }

    handle->ctrl1 = value;
    handle->range = range;
    qmc5883l_update_sensitivity(handle);
    return DRIVER_OK;
}

int qmc5883l_get_status(qmc5883l_handle_t *handle, uint8_t *status)
{
    if (handle == NULL || status == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    return qmc5883l_read_regs(handle, QMC5883L_REG_STATUS, status, 1U);
}

int qmc5883l_init(qmc5883l_handle_t *handle, iot_i2c_t *i2c, uint16_t address)
{
    uint8_t id = 0U;
    int ret;

    if (handle == NULL || i2c == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : QMC5883L_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->range = QMC5883L_RANGE_8G;
    handle->ctrl1 = QMC5883L_CTRL1_DEFAULT;
    handle->initialized = false;
    qmc5883l_update_sensitivity(handle);

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("qmc5883l", "probe failed at 0x%02X", (unsigned)handle->i2c.address);
        return (int)IOT_NOT_FOUND;
    }

    ret = qmc5883l_soft_reset(handle);
    if (ret != DRIVER_OK) {
        LOG_E("qmc5883l", "config failed at 0x%02X", (unsigned)handle->i2c.address);
        return ret;
    }

    /* 识别寄存器仅作参考，非致命 */
    if (qmc5883l_read_regs(handle, QMC5883L_REG_CHIP_ID, &id, 1U) == DRIVER_OK &&
        id != QMC5883L_CHIP_ID_VALUE) {
        LOG_W("qmc5883l", "chip id 0x%02X (datasheet 0xFF)", (unsigned)id);
    }

    driver_delay_ms(QMC5883L_START_DELAY_MS);
    handle->initialized = true;
    LOG_I("qmc5883l", "init ok at 0x%02X, range=%s",
          (unsigned)handle->i2c.address,
          (handle->range == QMC5883L_RANGE_8G) ? "8G" : "2G");
    return DRIVER_OK;
}

int qmc5883l_deinit(qmc5883l_handle_t *handle)
{
    if (handle == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    if (handle->initialized && handle->i2c.handle != NULL) {
        uint8_t standby = (uint8_t)(handle->ctrl1 & (uint8_t)~QMC5883L_CTRL1_MODE_MASK);
        (void)qmc5883l_write_reg(handle, QMC5883L_REG_CTRL1, standby);
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int qmc5883l_read_raw(qmc5883l_handle_t *handle, int16_t xyz[3])
{
    uint8_t data[QMC5883L_DATA_LEN];
    uint8_t status = 0U;
    uint32_t start;
    int ret;

    if (handle == NULL || xyz == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }

    /* 等一次转换完成：数据手册要求 DRDY 置位后再读 */
    start = driver_tick_ms();
    for (;;) {
        ret = qmc5883l_read_regs(handle, QMC5883L_REG_STATUS, &status, 1U);
        if (ret != DRIVER_OK) {
            return ret;
        }
        if ((status & QMC5883L_STATUS_DRDY) != 0U) {
            break;
        }
        if ((uint32_t)(driver_tick_ms() - start) > QMC5883L_DATA_TIMEOUT_MS) {
            return (int)IOT_TIMEOUT;
        }
        driver_delay_ms(1U);
    }

    ret = qmc5883l_read_regs(handle, QMC5883L_REG_DATA_X_LSB, data, sizeof(data));
    if (ret != DRIVER_OK) {
        return ret;
    }

    xyz[0] = driver_le16(&data[0]);
    xyz[1] = driver_le16(&data[2]);
    xyz[2] = driver_le16(&data[4]);
    return DRIVER_OK;
}

int qmc5883l_read(qmc5883l_handle_t *handle, qmc5883l_data_t *data)
{
    int16_t xyz[3];
    uint8_t status = 0U;
    int ret;

    if (handle == NULL || data == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }

    ret = qmc5883l_read_raw(handle, xyz);
    if (ret != DRIVER_OK) {
        return ret;
    }

    data->mag_ut.x = (float)xyz[0] * handle->ut_per_lsb;
    data->mag_ut.y = (float)xyz[1] * handle->ut_per_lsb;
    data->mag_ut.z = (float)xyz[2] * handle->ut_per_lsb;

    data->heading_deg = atan2f(data->mag_ut.y, data->mag_ut.x) * QMC5883L_RAD_TO_DEG;
    if (data->heading_deg < 0.0f) {
        data->heading_deg += 360.0f;
    }

    if (qmc5883l_read_regs(handle, QMC5883L_REG_STATUS, &status, 1U) == DRIVER_OK) {
        data->data_ready = ((status & QMC5883L_STATUS_DRDY) != 0U);
        data->overflow = ((status & QMC5883L_STATUS_OVL) != 0U);
    } else {
        data->data_ready = true;
        data->overflow = false;
    }
    return DRIVER_OK;
}
