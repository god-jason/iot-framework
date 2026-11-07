#include "apds9960.h"
#include "apds9960_register.h"
#include "log.h"

static uint8_t apds9960_again_code(apds9960_gain_t gain)
{
    switch (gain) {
    case APDS9960_AGAIN_4X:
        return APDS9960_AGAIN_CODE_4X;
    case APDS9960_AGAIN_16X:
        return APDS9960_AGAIN_CODE_16X;
    case APDS9960_AGAIN_64X:
        return APDS9960_AGAIN_CODE_64X;
    case APDS9960_AGAIN_1X:
    default:
        return APDS9960_AGAIN_CODE_1X;
    }
}

static uint8_t apds9960_control_value(const apds9960_handle_t *handle)
{
    /* 接近增益固定 4x，ALS 增益由句柄配置 */
    return (uint8_t)(apds9960_again_code(handle->gain) | APDS9960_PGAIN_4X);
}

static uint8_t apds9960_ppulse_value(const apds9960_handle_t *handle)
{
    return (uint8_t)(handle->pulse_len |
                     (uint8_t)(handle->pulse_count & APDS9960_PPULSE_COUNT_MASK));
}

static int apds9960_write_reg(apds9960_handle_t *handle, uint8_t reg, uint8_t value)
{
    return driver_i2c_write_reg(&handle->i2c, reg, &value, 1U);
}

/* 轮询 STATUS 中的有效标志，避免依赖不精确的固定延时 */
static int apds9960_wait_status(apds9960_handle_t *handle, uint8_t mask)
{
    uint32_t start = driver_tick_ms();
    uint8_t status = 0U;

    for (;;) {
        if (driver_i2c_read_reg(&handle->i2c, APDS9960_REG_STATUS, &status, 1U) != DRIVER_OK) {
            return (int)IOT_IO_ERROR;
        }
        if ((status & mask) != 0U) {
            return DRIVER_OK;
        }
        if ((uint32_t)(driver_tick_ms() - start) >= APDS9960_POLL_TIMEOUT_MS) {
            return (int)IOT_TIMEOUT;
        }
        driver_delay_ms(1U);
    }
}

float apds9960_get_integration_time_ms(const apds9960_handle_t *handle)
{
    if (handle == NULL) {
        return 0.0f;
    }
    return (float)(256U - (uint32_t)handle->atime) * APDS9960_ALS_STEP_MS;
}

int apds9960_init(apds9960_handle_t *handle, iot_i2c_t *i2c, uint16_t address)
{
    uint8_t id = 0U;
    int ret;

    if (handle == NULL || i2c == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : APDS9960_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->atime = APDS9960_ATIME_DEFAULT;
    handle->gain = APDS9960_AGAIN_4X;
    handle->pulse_count = 8U;
    handle->pulse_len = APDS9960_PPULSE_LEN_16US;
    handle->initialized = false;

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("apds9960", "probe failed at 0x%02X", handle->i2c.address);
        return (int)IOT_NOT_FOUND;
    }

    ret = driver_i2c_read_reg(&handle->i2c, APDS9960_REG_ID, &id, 1U);
    if (ret != DRIVER_OK) {
        return ret;
    }
    if (id != APDS9960_ID_APDS9960) {
        LOG_E("apds9960", "unexpected id 0x%02X", id);
        return (int)IOT_NOT_FOUND;
    }

    /* 先关闭所有引擎再配置，避免旧配置残留 */
    if (apds9960_write_reg(handle, APDS9960_REG_ENABLE, 0x00U) != DRIVER_OK ||
        apds9960_write_reg(handle, APDS9960_REG_ATIME, handle->atime) != DRIVER_OK ||
        apds9960_write_reg(handle, APDS9960_REG_CONTROL,
                           apds9960_control_value(handle)) != DRIVER_OK ||
        apds9960_write_reg(handle, APDS9960_REG_PPULSE,
                           apds9960_ppulse_value(handle)) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }

    if (apds9960_write_reg(handle, APDS9960_REG_ENABLE, APDS9960_ENABLE_PON) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    driver_delay_ms(APDS9960_POWER_ON_DELAY_MS);

    if (apds9960_write_reg(handle, APDS9960_REG_ENABLE,
                           APDS9960_ENABLE_PON | APDS9960_ENABLE_AEN |
                               APDS9960_ENABLE_PEN) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }

    handle->initialized = true;
    LOG_I("apds9960", "init ok at 0x%02X (id=0x%02X)", handle->i2c.address, id);
    return DRIVER_OK;
}

int apds9960_deinit(apds9960_handle_t *handle)
{
    if (handle == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    if (handle->i2c.handle != NULL) {
        (void)apds9960_write_reg(handle, APDS9960_REG_ENABLE, 0x00U);
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int apds9960_set_atime(apds9960_handle_t *handle, uint8_t atime)
{
    if (handle == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    if (apds9960_write_reg(handle, APDS9960_REG_ATIME, atime) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    handle->atime = atime;
    driver_delay_ms((uint32_t)apds9960_get_integration_time_ms(handle) + 2U);
    return DRIVER_OK;
}

int apds9960_set_als_gain(apds9960_handle_t *handle, apds9960_gain_t gain)
{
    if (handle == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    if ((unsigned int)gain > (unsigned int)APDS9960_AGAIN_64X) {
        return (int)IOT_INVALID_PARAM;
    }
    handle->gain = gain;
    if (apds9960_write_reg(handle, APDS9960_REG_CONTROL,
                           apds9960_control_value(handle)) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    driver_delay_ms((uint32_t)apds9960_get_integration_time_ms(handle) + 2U);
    return DRIVER_OK;
}

int apds9960_set_proximity_pulse(apds9960_handle_t *handle, uint8_t pulse_count,
                                 uint8_t pulse_len)
{
    if (handle == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    if (pulse_count == 0U || pulse_count > 64U) {
        return (int)IOT_INVALID_PARAM;
    }
    if (pulse_len != APDS9960_PPULSE_LEN_4US && pulse_len != APDS9960_PPULSE_LEN_8US &&
        pulse_len != APDS9960_PPULSE_LEN_16US && pulse_len != APDS9960_PPULSE_LEN_32US) {
        return (int)IOT_INVALID_PARAM;
    }

    handle->pulse_count = pulse_count;
    handle->pulse_len = pulse_len;
    if (apds9960_write_reg(handle, APDS9960_REG_PPULSE,
                           apds9960_ppulse_value(handle)) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    return DRIVER_OK;
}

int apds9960_read_rgbc(apds9960_handle_t *handle, apds9960_rgbc_t *rgbc)
{
    uint8_t buf[8] = { 0U };
    int ret;

    if (handle == NULL || rgbc == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }

    /* 等待一个完整 ALS 积分周期，再用 AVALID 确认数据就绪 */
    driver_delay_ms((uint32_t)apds9960_get_integration_time_ms(handle) + 2U);
    ret = apds9960_wait_status(handle, APDS9960_STATUS_AVALID);
    if (ret != DRIVER_OK) {
        return ret;
    }

    /* 0x94~0x9B：C/R/G/B 各 2 字节，小端，多字节读自动递增 */
    ret = driver_i2c_read_reg(&handle->i2c, APDS9960_REG_CDATAL, buf, sizeof(buf));
    if (ret != DRIVER_OK) {
        return ret;
    }

    rgbc->clear = (uint16_t)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8));
    rgbc->red   = (uint16_t)((uint16_t)buf[2] | ((uint16_t)buf[3] << 8));
    rgbc->green = (uint16_t)((uint16_t)buf[4] | ((uint16_t)buf[5] << 8));
    rgbc->blue  = (uint16_t)((uint16_t)buf[6] | ((uint16_t)buf[7] << 8));
    rgbc->saturated = (rgbc->clear == 0xFFFFU) || (rgbc->red == 0xFFFFU) ||
                      (rgbc->green == 0xFFFFU) || (rgbc->blue == 0xFFFFU);
    return DRIVER_OK;
}

int apds9960_read_proximity(apds9960_handle_t *handle, uint8_t *proximity, bool *saturated)
{
    uint8_t value = 0U;
    int ret;

    if (handle == NULL || proximity == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }

    ret = apds9960_wait_status(handle, APDS9960_STATUS_PVALID);
    if (ret != DRIVER_OK) {
        return ret;
    }

    ret = driver_i2c_read_reg(&handle->i2c, APDS9960_REG_PDATA, &value, 1U);
    if (ret != DRIVER_OK) {
        return ret;
    }

    *proximity = value;
    if (saturated != NULL) {
        *saturated = (value == 0xFFU);
    }
    return DRIVER_OK;
}

int apds9960_read(apds9960_handle_t *handle, apds9960_data_t *data)
{
    int ret;

    if (handle == NULL || data == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    ret = apds9960_read_rgbc(handle, &data->rgbc);
    if (ret != DRIVER_OK) {
        return ret;
    }
    return apds9960_read_proximity(handle, &data->proximity, &data->proximity_saturated);
}
