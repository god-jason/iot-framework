#include "ads1110.h"
#include "ads1110_register.h"
#include "log.h"

/* 各速率对应的转换周期（ms） */
static const uint16_t ads1110_conv_ms[4] = {
    ADS1110_CONV_MS_15SPS, ADS1110_CONV_MS_30SPS,
    ADS1110_CONV_MS_60SPS, ADS1110_CONV_MS_240SPS
};

static uint8_t ads1110_config(const ads1110_handle_t *handle, bool start)
{
    uint8_t config;

    config = (uint8_t)(((uint8_t)handle->data_rate << ADS1110_CFG_DR_SHIFT) & ADS1110_CFG_DR_MASK);
    config |= (uint8_t)(((uint8_t)handle->gain << ADS1110_CFG_SC_SHIFT) & ADS1110_CFG_SC_MASK);
    if (start) {
        config |= ADS1110_CFG_ST;
    }
    return config;
}

static int ads1110_write_config(ads1110_handle_t *handle, uint8_t config)
{
    if (driver_i2c_write(&handle->i2c, &config, 1U) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    return DRIVER_OK;
}

int ads1110_init(ads1110_handle_t *handle, hal_i2c_t *i2c, uint16_t address)
{
    uint8_t buf[ADS1110_READ_LEN] = { 0U, 0U, 0U };

    if (handle == NULL || i2c == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : ADS1110_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->gain = ADS1110_GAIN_1;
    handle->data_rate = ADS1110_RATE_240SPS;
    handle->initialized = false;

    if (handle->i2c.address > ADS1110_ADDRESS_MAX) {
        return (int)HAL_INVALID_PARAM;
    }

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("ads1110", "probe failed at 0x%02X", handle->i2c.address);
        return (int)HAL_NOT_FOUND;
    }

    /* 读 3 字节：数据 + 配置回读，保留位必须为 0 */
    if (driver_i2c_read(&handle->i2c, buf, ADS1110_READ_LEN) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    if ((buf[2] & ADS1110_CFG_RESERVED_MASK) != 0U) {
        LOG_E("ads1110", "invalid config 0x%02X", (unsigned)buf[2]);
        return (int)HAL_NOT_FOUND;
    }

    if (ads1110_write_config(handle, ads1110_config(handle, false)) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }

    handle->initialized = true;
    LOG_I("ads1110", "init ok at 0x%02X", handle->i2c.address);
    return DRIVER_OK;
}

int ads1110_deinit(ads1110_handle_t *handle)
{
    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int ads1110_set_gain(ads1110_handle_t *handle, ads1110_gain_e gain)
{
    int ret;

    if (handle == NULL || !handle->initialized || (uint16_t)gain > (uint16_t)ADS1110_GAIN_8) {
        return (int)HAL_INVALID_PARAM;
    }

    handle->gain = gain;
    ret = ads1110_write_config(handle, ads1110_config(handle, false));
    return ret;
}

int ads1110_set_data_rate(ads1110_handle_t *handle, ads1110_rate_e data_rate)
{
    int ret;

    if (handle == NULL || !handle->initialized ||
        (uint16_t)data_rate > (uint16_t)ADS1110_RATE_240SPS) {
        return (int)HAL_INVALID_PARAM;
    }

    handle->data_rate = data_rate;
    ret = ads1110_write_config(handle, ads1110_config(handle, false));
    return ret;
}

int ads1110_start(ads1110_handle_t *handle)
{
    if (handle == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    return ads1110_write_config(handle, ads1110_config(handle, true));
}

int ads1110_read(ads1110_handle_t *handle, int16_t *raw)
{
    uint8_t buf[ADS1110_READ_LEN] = { 0U, 0U, 0U };
    int ret;

    if (handle == NULL || raw == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }

    ret = ads1110_start(handle);
    if (ret != DRIVER_OK) {
        return ret;
    }
    driver_delay_ms((uint32_t)ads1110_conv_ms[handle->data_rate] + ADS1110_CONV_EXTRA_MS);

    if (driver_i2c_read(&handle->i2c, buf, ADS1110_READ_LEN) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    if ((buf[2] & ADS1110_CFG_RESERVED_MASK) != 0U) {
        return (int)HAL_IO_ERROR;   /* 总线异常，回读不是有效配置 */
    }

    *raw = (int16_t)(((uint16_t)buf[0] << 8) | (uint16_t)buf[1]);
    return DRIVER_OK;
}

int ads1110_read_voltage(ads1110_handle_t *handle, float *volts)
{
    int16_t raw = 0;
    float gain;
    int ret;

    if (handle == NULL || volts == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }

    ret = ads1110_read(handle, &raw);
    if (ret != DRIVER_OK) {
        return ret;
    }

    gain = (float)(1U << (uint8_t)handle->gain);
    *volts = (float)raw * (float)ADS1110_VREF_MV / ADS1110_CODE_FULL_SCALE / gain / 1000.0f;
    return DRIVER_OK;
}

int ads1110_get_config(ads1110_handle_t *handle, uint8_t *config)
{
    uint8_t buf[ADS1110_READ_LEN] = { 0U, 0U, 0U };

    if (handle == NULL || config == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }

    if (driver_i2c_read(&handle->i2c, buf, ADS1110_READ_LEN) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    *config = buf[2];
    return DRIVER_OK;
}
