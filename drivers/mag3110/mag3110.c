#include "mag3110.h"
#include "mag3110_register.h"
#include "log.h"
#include <math.h>

static int mag3110_write(mag3110_handle_t *handle, uint8_t reg, uint8_t value)
{
    return (driver_i2c_write_reg(&handle->i2c, reg, &value, 1U) == DRIVER_OK) ? DRIVER_OK
                                                                              : (int)HAL_IO_ERROR;
}

static int mag3110_read_reg(mag3110_handle_t *handle, uint8_t reg, uint8_t *buf, size_t len)
{
    return (driver_i2c_read_reg(&handle->i2c, reg, buf, len) == DRIVER_OK) ? DRIVER_OK
                                                                          : (int)HAL_IO_ERROR;
}

int mag3110_get_status(mag3110_handle_t *handle, uint8_t *status)
{
    if (handle == NULL || status == NULL || !handle->initialized) { return (int)HAL_INVALID_PARAM; }
    return mag3110_read_reg(handle, MAG3110_REG_DR_STATUS, status, 1U);
}

/* 由数据率与过采样合成 CTRL_REG1，保留激活位 */
static uint8_t mag3110_ctrl1_value(const mag3110_handle_t *handle)
{
    return (uint8_t)(((uint8_t)handle->data_rate << MAG3110_CTRL1_DR_SHIFT) |
                     ((uint8_t)handle->oversampling << MAG3110_CTRL1_OS_SHIFT) |
                     MAG3110_CTRL1_AC);
}

int mag3110_set_data_rate(mag3110_handle_t *handle, mag3110_data_rate_t data_rate)
{
    uint8_t check = 0U;
    uint8_t value;
    if (handle == NULL || !handle->initialized || (unsigned)data_rate >= (unsigned)MAG3110_DR_COUNT) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->data_rate = data_rate;
    value = mag3110_ctrl1_value(handle);
    if (mag3110_write(handle, MAG3110_REG_CTRL_REG1, value) != DRIVER_OK) { return (int)HAL_IO_ERROR; }
    if (mag3110_read_reg(handle, MAG3110_REG_CTRL_REG1, &check, 1U) != DRIVER_OK ||
        (check & MAG3110_CTRL1_DR_MASK) != (value & MAG3110_CTRL1_DR_MASK)) {
        LOG_E("mag3110", "ctrl_reg1 readback 0x%02X mismatch", check);
        return (int)HAL_HW_ERROR;
    }
    return DRIVER_OK;
}

int mag3110_init(mag3110_handle_t *handle, hal_i2c_t *i2c, uint16_t address,
                 mag3110_data_rate_t data_rate, mag3110_oversampling_t oversampling)
{
    uint8_t id = 0U;
    uint8_t check = 0U;
    uint8_t value;
    if (handle == NULL || i2c == NULL ||
        (unsigned)data_rate >= (unsigned)MAG3110_DR_COUNT ||
        (unsigned)oversampling >= (unsigned)MAG3110_OS_COUNT) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->i2c = (driver_i2c_t){ .handle = i2c, .timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT,
                                  .address = (address != 0U) ? address : MAG3110_DEFAULT_ADDRESS };
    handle->data_rate = data_rate;
    handle->oversampling = oversampling;
    handle->initialized = false;

    /* WHO_AM_I 在位校验 */
    if (mag3110_read_reg(handle, MAG3110_REG_WHO_AM_I, &id, 1U) != DRIVER_OK) { return (int)HAL_IO_ERROR; }
    if (id != MAG3110_WHO_AM_I_VALUE) {
        LOG_E("mag3110", "who_am_i 0x%02X mismatch", id);
        return (int)HAL_NOT_FOUND;
    }

    /* 配置前先进入待机（AC = 0） */
    if (mag3110_write(handle, MAG3110_REG_CTRL_REG1, 0x00U) != DRIVER_OK) { return (int)HAL_IO_ERROR; }
    /* 使能自动磁复位，避免外部磁场冲击后零点漂移 */
    if (mag3110_write(handle, MAG3110_REG_CTRL_REG2, MAG3110_CTRL2_AUTO_MRST_EN) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }

    value = mag3110_ctrl1_value(handle);
    if (mag3110_write(handle, MAG3110_REG_CTRL_REG1, value) != DRIVER_OK) { return (int)HAL_IO_ERROR; }
    driver_delay_ms(MAG3110_START_DELAY_MS);

    /* 写通路自检：回读 CTRL_REG1 与 SYSMOD */
    if (mag3110_read_reg(handle, MAG3110_REG_CTRL_REG1, &check, 1U) != DRIVER_OK ||
        (check & (MAG3110_CTRL1_DR_MASK | MAG3110_CTRL1_OS_MASK | MAG3110_CTRL1_AC)) != value) {
        LOG_E("mag3110", "ctrl_reg1 readback 0x%02X mismatch", check);
        return (int)HAL_HW_ERROR;
    }
    if (mag3110_read_reg(handle, MAG3110_REG_SYSMOD, &check, 1U) != DRIVER_OK ||
        (check & MAG3110_SYSMOD_ACTIVE) == 0U) {
        LOG_E("mag3110", "failed to enter active mode (sysmod 0x%02X)", check);
        return (int)HAL_HW_ERROR;
    }

    handle->initialized = true;
    LOG_I("mag3110", "init ok at 0x%02X, dr=%d os=%d", (int)handle->i2c.address,
          (int)data_rate, (int)oversampling);
    return DRIVER_OK;
}

int mag3110_deinit(mag3110_handle_t *handle)
{
    if (handle == NULL) { return (int)HAL_INVALID_PARAM; }
    if (handle->initialized) { (void)mag3110_write(handle, MAG3110_REG_CTRL_REG1, 0x00U); }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int mag3110_read_raw(mag3110_handle_t *handle, int16_t xyz[3])
{
    uint8_t d[MAG3110_DATA_BURST_LEN];
    uint8_t status = 0U;
    uint32_t start;
    if (handle == NULL || xyz == NULL || !handle->initialized) { return (int)HAL_INVALID_PARAM; }

    /* 等待 ZYXDR，同时检查溢出位 */
    start = driver_tick_ms();
    do {
        if (mag3110_read_reg(handle, MAG3110_REG_DR_STATUS, &status, 1U) != DRIVER_OK) { return (int)HAL_IO_ERROR; }
        if ((status & MAG3110_DR_STATUS_ZYXDR) != 0U) { break; }
        driver_delay_ms(1U);
    } while ((uint32_t)(driver_tick_ms() - start) <= MAG3110_DATA_TIMEOUT_MS);
    if ((status & MAG3110_DR_STATUS_ZYXDR) == 0U) { return (int)HAL_TIMEOUT; }
    if ((status & (MAG3110_DR_STATUS_ZYXOW | MAG3110_DR_STATUS_ZOW |
                   MAG3110_DR_STATUS_YOW | MAG3110_DR_STATUS_XOW)) != 0U) {
        LOG_W("mag3110", "data overrun, status 0x%02X", status);
    }

    if (mag3110_read_reg(handle, MAG3110_REG_OUT_X_MSB, d, sizeof(d)) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    /* 大端：X/Y/Z 依次为 MSB, LSB */
    xyz[0] = driver_be16(&d[0]);
    xyz[1] = driver_be16(&d[2]);
    xyz[2] = driver_be16(&d[4]);
    return DRIVER_OK;
}

int mag3110_read(mag3110_handle_t *handle, mag3110_data_t *data)
{
    int16_t xyz[3];
    uint8_t temp = 0U;
    int ret;
    if (handle == NULL || data == NULL) { return (int)HAL_INVALID_PARAM; }
    ret = mag3110_read_raw(handle, xyz);
    if (ret != DRIVER_OK) { return ret; }
    if (mag3110_read_reg(handle, MAG3110_REG_DIE_TEMP, &temp, 1U) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }

    data->mag_ut.x = (float)xyz[0] * MAG3110_UT_PER_LSB;
    data->mag_ut.y = (float)xyz[1] * MAG3110_UT_PER_LSB;
    data->mag_ut.z = (float)xyz[2] * MAG3110_UT_PER_LSB;
    data->heading_deg = atan2f(data->mag_ut.y, data->mag_ut.x) * 57.2957795f;
    if (data->heading_deg < 0.0f) { data->heading_deg += 360.0f; }
    data->die_temp_c = (float)(int8_t)temp * MAG3110_DEGC_PER_LSB;
    return DRIVER_OK;
}
